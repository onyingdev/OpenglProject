#pragma once
#include "Render/model.h"
#include <memory>
#include <string>
#include <unordered_map>

// Path-keyed model cache. A Model load is expensive — a synchronous Assimp import plus a GPU upload
// (VAO/VBO + textures) — so loading the SAME model repeatedly (every cube the spawner drops, every
// duplicate in a scene file) re-imports and re-uploads identical geometry, hitching the frame.
//
// get() loads on the first miss and hands back a shared_ptr<Model> that every later request for the
// same (path, gamma, flip) reuses: one import, one GPU upload, shared by all instances. Sharing is
// safe because a Model is just geometry/textures/draw state — the per-instance world matrix lives in
// the RenderItem, not the Model. MeshComponent::deserialize routes through here; the explicit
// MeshComponent(path) constructor still loads a fresh, un-shared Model for callers that want one.
//
// LIFETIME: Model/Mesh destructors call glDelete*, so the cache's shared_ptrs must be dropped while
// the GL context is alive — clear() is called from Engine::shutdown() (and SceneManager::unloadScenes
// for hot-reload freshness). Clearing is always safe: a Model still referenced by a live MeshComponent
// survives via shared ownership; only the cache's own reference is dropped.
//
// Single-threaded: all model loads happen on the main thread. The inline functions share one
// function-local static registry across every translation unit (guaranteed for inline functions).
namespace ModelCache {

    inline std::unordered_map<std::string, std::shared_ptr<Model>>& registry() {
        static std::unordered_map<std::string, std::shared_ptr<Model>> r;
        return r;
    }

    inline std::shared_ptr<Model> get(const std::string& path, bool gamma = false, bool flipUVs = false) {
        // Key by path + variant flags so e.g. a flipped and an un-flipped load don't alias.
        std::string key = path + (gamma ? "|g1" : "|g0") + (flipUVs ? "|f1" : "|f0");
        auto& r = registry();
        auto it = r.find(key);
        if (it != r.end()) return it->second;                       // hit: reuse the uploaded model
        auto model = std::make_shared<Model>(path, gamma, flipUVs); // miss: import + upload once
        r.emplace(std::move(key), model);
        return model;
    }

    // Drop the cache's references. Models still held by a live MeshComponent stay alive (shared_ptr);
    // those whose only owner was the cache are freed here — so call it while the GL context is alive.
    inline void clear() { registry().clear(); }

}
