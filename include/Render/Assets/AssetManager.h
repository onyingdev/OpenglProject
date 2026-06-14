#pragma once
#include "Render/model.h"               // Model::parse (CPU) + Model(ModelData&&) (GL upload)
#include "Render/Assets/ModelData.h"
#include "Render/API/IModel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Async, threaded asset loader + unified model cache. The expensive CPU half of a model load
// (Assimp parse + stb_image decode -> ModelData) runs on worker threads; the GL half (upload) runs on
// the main thread via pump(), because OpenGL is single-context/single-thread. Splitting the load this
// way moves the spike-causing parse off the render thread, so a heavy model no longer hitches the frame.
//
// Threading contract:
//   - m_cache / m_inFlight are touched ONLY on the main thread (getSync / request / pump are all called
//     from the main thread), so they need no locking.
//   - Workers touch ONLY the request/completion queues (each mutex-guarded) and call Model::parse,
//     which is GL-free. Each worker owns its own Assimp::Importer; stb_image's only global (the flip
//     flag) is set once at startup and never changed, so concurrent stbi_load is safe.
//
// Lifetime: cached Models hold GL objects, so the cache must be dropped while the context is alive.
// Engine::shutdown() calls shutdown() (stops workers + clears the cache); SceneManager hot-reload calls
// clearCache(). Workers start lazily on the first async request — zero threads/overhead if unused.

// Resolves to an uploaded model on a later frame. Held (shared) by whoever requested it (a
// MeshComponent). Mutated only on the main thread (by pump), so no atomics needed.
struct ModelHandle {
    std::shared_ptr<IModel> model;   // null until ready
    bool        ready = false;
    std::string key;
};

class AssetManager {
public:
    static AssetManager& instance() { static AssetManager m; return m; }

    // --- Synchronous load (blocking parse + upload on the calling thread, cached). What scene-load /
    //     ModelCache::get uses; preserves the old instant-load behavior. ---
    std::shared_ptr<IModel> getSync(const std::string& path, bool gamma = false, bool flip = false) {
        const std::string key = makeKey(path, gamma, flip);
        auto it = m_cache.find(key);
        if (it != m_cache.end()) return it->second;
        std::shared_ptr<IModel> model = std::make_shared<Model>(path, gamma, flip);
        m_cache.emplace(key, model);
        return model;
    }

    // --- Asynchronous load. Returns immediately with a handle that resolves once the model has been
    //     parsed (worker) and uploaded (pump). Dedups concurrent requests + cache hits. ---
    std::shared_ptr<ModelHandle> request(const std::string& path, bool gamma = false, bool flip = false) {
        const std::string key = makeKey(path, gamma, flip);

        // Already uploaded -> a handle that's ready right now (no pop-in).
        if (auto cit = m_cache.find(key); cit != m_cache.end()) {
            auto h = std::make_shared<ModelHandle>();
            h->key = key; h->model = cit->second; h->ready = true;
            return h;
        }
        // Already being loaded -> share the in-flight handle (one parse+upload for N requesters).
        if (auto fit = m_inFlight.find(key); fit != m_inFlight.end()) return fit->second;

        ensureWorkers();
        auto h = std::make_shared<ModelHandle>();
        h->key = key;
        m_inFlight.emplace(key, h);
        {
            std::lock_guard<std::mutex> lk(m_reqMutex);
            m_reqQueue.push(Job{ key, path, gamma, flip });
        }
        m_reqCv.notify_one();
        return h;
    }

    // --- Per-frame upload pump (main / GL thread). Uploads parsed models within a time budget so a big
    //     batch is spread across frames instead of stalling one. Always uploads at least one if ready. ---
    void pump(double budgetMs = 2.0) {
        using clock = std::chrono::high_resolution_clock;
        const auto start = clock::now();
        for (;;) {
            Result r;
            {
                std::lock_guard<std::mutex> lk(m_doneMutex);
                if (m_doneQueue.empty()) break;
                r = std::move(m_doneQueue.front());
                m_doneQueue.pop();
            }
            std::shared_ptr<IModel> model = std::make_shared<Model>(std::move(r.data));  // GL upload
            m_cache[r.key] = model;
            if (auto fit = m_inFlight.find(r.key); fit != m_inFlight.end()) {
                fit->second->model = model;
                fit->second->ready = true;
                m_inFlight.erase(fit);
            }
            if (std::chrono::duration<double, std::milli>(clock::now() - start).count() >= budgetMs)
                break;
        }
    }

    // Drop the cached models (frees their GL objects). Call only while the GL context is alive.
    void clearCache() { m_cache.clear(); }

    // Full teardown: stop the workers, discard pending parses, drop the cache. Call from
    // Engine::shutdown() before the GL context goes away.
    void shutdown() {
        stopWorkers();
        { std::lock_guard<std::mutex> lk(m_doneMutex); std::queue<Result> empty; m_doneQueue.swap(empty); }
        clearCache();
        m_inFlight.clear();
    }

private:
    struct Job    { std::string key, path; bool gamma, flip; };
    struct Result { std::string key; ModelData data; };

    static std::string makeKey(const std::string& path, bool gamma, bool flip) {
        return path + (gamma ? "|g1" : "|g0") + (flip ? "|f1" : "|f0");
    }

    void ensureWorkers() {
        if (m_started) return;
        m_started = true;
        m_running = true;
        // A couple of loader threads is plenty — the bottleneck is disk + the main-thread upload pump.
        const unsigned n = std::max(1u, std::thread::hardware_concurrency() / 2u);
        for (unsigned i = 0; i < n; ++i) m_workers.emplace_back([this] { workerLoop(); });
    }

    void workerLoop() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(m_reqMutex);
                m_reqCv.wait(lk, [this] { return !m_running || !m_reqQueue.empty(); });
                if (!m_running && m_reqQueue.empty()) return;
                job = std::move(m_reqQueue.front());
                m_reqQueue.pop();
            }
            ModelData data = Model::parse(job.path, job.gamma, job.flip);   // CPU only — no GL
            {
                std::lock_guard<std::mutex> lk(m_doneMutex);
                m_doneQueue.push(Result{ job.key, std::move(data) });
            }
        }
    }

    void stopWorkers() {
        if (!m_started) return;
        { std::lock_guard<std::mutex> lk(m_reqMutex); m_running = false; }
        m_reqCv.notify_all();
        for (auto& t : m_workers) if (t.joinable()) t.join();
        m_workers.clear();
        m_started = false;
    }

    ~AssetManager() { stopWorkers(); }   // safety net; Engine::shutdown() should have run already

    // Main-thread-only state (no locking — see threading contract above).
    std::unordered_map<std::string, std::shared_ptr<IModel>>     m_cache;     // ready, uploaded models
    std::unordered_map<std::string, std::shared_ptr<ModelHandle>> m_inFlight; // pending async loads

    // Cross-thread queues (each guarded by its mutex).
    std::queue<Job>    m_reqQueue;  std::mutex m_reqMutex;  std::condition_variable m_reqCv;
    std::queue<Result> m_doneQueue; std::mutex m_doneMutex;

    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running{ false };
    bool m_started = false;
};
