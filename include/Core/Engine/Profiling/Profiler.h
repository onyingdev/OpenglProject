#pragma once
#include <glad/glad.h>
#include <chrono>
#include <string>
#include <vector>
#include <iostream>

// General, reusable frame profiler. Two independent backends, each with its own toggle:
//   - CPU: std::chrono around a section -> CPU/driver SUBMISSION cost (GL is async, so this is the
//          cost of issuing the work, not the GPU running it).
//   - GPU: glQueryCounter(GL_TIMESTAMP) at begin/end -> true GPU execution cost. Resolved one frame
//          later (double-buffered) so reading a result never stalls the CPU on the GPU.
// Sections carry a ProfLevel; Detail sections only record when DetailEnabled, so coarse frame stages
// and fine renderer subsections can be toggled separately. Results are averaged over ReportEvery
// frames and printed, mirroring the physics [phys2] line (RigidBodyComponent3D::reportTimings).
//
// GL note: GL_TIME_ELAPSED queries can't nest/overlap (one active at a time); GL_TIMESTAMP counters
// can, which is why a coarse gpu.frame span can wrap the gpu.flush/resolve/present subsections.
//
// Usage:
//   PROFILE_CPU("draw");                       // RAII, whole enclosing scope
//   PROFILE_GPU_DETAIL("gpu.flush");           // RAII GPU subsection
//   Profiler::instance().gpuBegin("gpu.frame");// explicit pair when the span crosses functions
//   ... Profiler::instance().gpuEnd("gpu.frame");
//   Profiler::instance().endFrame();           // once per frame, after present()

enum class ProfLevel { Coarse, Detail };

class Profiler {
public:
    static Profiler& instance() { static Profiler p; return p; }

    // Independent toggles, set from render.json at startup (mirrors render.debugColliders).
    inline static bool CpuEnabled    = false;   // chrono submission timing
    inline static bool GpuEnabled    = false;   // GL_TIMESTAMP GPU timing
    inline static bool DetailEnabled = false;   // gates ProfLevel::Detail sections (both backends)
    inline static int  ReportEvery   = 60;      // frames per averaged print

    // --- CPU sections (chrono; nestable via a stack) ---
    void cpuBegin(const char* name, ProfLevel lvl = ProfLevel::Coarse) {
        m_cpuStack.push_back({ name, lvl, Clock::now() });
    }
    void cpuEnd(const char* /*name*/) {
        if (m_cpuStack.empty()) return;
        const CpuFrame f = m_cpuStack.back();
        m_cpuStack.pop_back();
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - f.start).count();
        slot(m_cpu, f.name, f.level).accumMs += ms;
    }

    // --- GPU sections (GL_TIMESTAMP; nestable; resolved one frame later) ---
    // The GpuEnabled guard is here (not just in GpuScope) because gpu.frame is opened with an explicit
    // gpuBegin/gpuEnd pair that doesn't go through the scope's toggle check — without it, GL queries
    // would still be issued every frame when GPU profiling is off.
    void gpuBegin(const char* name, ProfLevel lvl = ProfLevel::Coarse) {
        if (!GpuEnabled) return;
        if (lvl == ProfLevel::Detail && !DetailEnabled) return;   // mirror the scope's record gate
        Entry e;
        e.name    = name;
        e.level   = lvl;
        e.beginQ  = acquireQuery();
        e.endQ    = acquireQuery();
        glQueryCounter(e.beginQ, GL_TIMESTAMP);
        m_cur.push_back(e);
    }
    void gpuEnd(const char* name) {
        if (!GpuEnabled) return;
        // Close the most recent still-open entry with this name (matches the begin even when nested).
        for (auto it = m_cur.rbegin(); it != m_cur.rend(); ++it) {
            if (!it->closed && it->name == name) {
                glQueryCounter(it->endQ, GL_TIMESTAMP);
                it->closed = true;
                return;
            }
        }
    }

    // Call once per frame after present(): resolve the PREVIOUS frame's GPU queries (now guaranteed
    // done), accumulate, then ping-pong the current frame's entries into the pending slot. Prints an
    // averaged report every ReportEvery frames.
    void endFrame() {
        // 1. Resolve last frame's GPU timestamps into the accumulators (queries are complete by now).
        for (const Entry& e : m_pending) {
            if (!e.closed) { recycle(e.beginQ); recycle(e.endQ); continue; }
            GLuint64 t0 = 0, t1 = 0;
            glGetQueryObjectui64v(e.beginQ, GL_QUERY_RESULT, &t0);
            glGetQueryObjectui64v(e.endQ,   GL_QUERY_RESULT, &t1);
            recycle(e.beginQ); recycle(e.endQ);
            slot(m_gpu, e.name, e.level).accumMs += (t1 >= t0) ? double(t1 - t0) / 1.0e6 : 0.0;
        }
        m_pending.swap(m_cur);   // this frame's entries become next frame's "pending"
        m_cur.clear();

        // 2. Average + print every ReportEvery frames.
        if (++m_frames < ReportEvery) return;
        const double inv = 1.0 / double(m_frames);
        if (CpuEnabled) { printLine("[prof-cpu]",        m_cpu, ProfLevel::Coarse, inv);
                          printLine("[prof-cpu-detail]", m_cpu, ProfLevel::Detail, inv); }
        if (GpuEnabled) { printLine("[prof-gpu]",        m_gpu, ProfLevel::Coarse, inv);
                          printLine("[prof-gpu-detail]", m_gpu, ProfLevel::Detail, inv); }
        for (Acc& a : m_cpu) a.accumMs = 0.0;
        for (Acc& a : m_gpu) a.accumMs = 0.0;
        m_frames = 0;
    }

    // Free GL query objects while the context is still alive (call from Engine::shutdown).
    void shutdownGL() {
        for (const Entry& e : m_cur)     { glDeleteQueries(1, &e.beginQ); glDeleteQueries(1, &e.endQ); }
        for (const Entry& e : m_pending) { glDeleteQueries(1, &e.beginQ); glDeleteQueries(1, &e.endQ); }
        for (GLuint q : m_freeQueries)   glDeleteQueries(1, &q);
        m_cur.clear(); m_pending.clear(); m_freeQueries.clear();
    }

private:
    using Clock = std::chrono::high_resolution_clock;

    struct CpuFrame { const char* name; ProfLevel level; Clock::time_point start; };
    struct Acc      { std::string name; ProfLevel level; double accumMs = 0.0; };
    struct Entry    { std::string name; ProfLevel level; GLuint beginQ = 0, endQ = 0; bool closed = false; };

    // Ordered accumulator lookup (insert on first sight) so the printout keeps stage order.
    Acc& slot(std::vector<Acc>& v, const std::string& name, ProfLevel lvl) {
        for (Acc& a : v) if (a.name == name) return a;
        v.push_back({ name, lvl, 0.0 });
        return v.back();
    }

    GLuint acquireQuery() {
        if (!m_freeQueries.empty()) { GLuint q = m_freeQueries.back(); m_freeQueries.pop_back(); return q; }
        GLuint q = 0; glGenQueries(1, &q); return q;
    }
    void recycle(GLuint q) { if (q) m_freeQueries.push_back(q); }

    void printLine(const char* tag, const std::vector<Acc>& v, ProfLevel lvl, double inv) {
        bool any = false;
        for (const Acc& a : v) if (a.level == lvl) { any = true; break; }
        if (!any) return;
        std::cout << tag;
        for (const Acc& a : v)
            if (a.level == lvl) std::cout << "  " << a.name << '=' << (a.accumMs * inv) << "ms";
        std::cout << "  (avg/" << m_frames << ")\n";
    }

    std::vector<CpuFrame> m_cpuStack;   // open CPU sections
    std::vector<Acc>      m_cpu, m_gpu; // ordered accumulators
    std::vector<Entry>    m_cur;        // this frame's GPU spans
    std::vector<Entry>    m_pending;    // last frame's GPU spans (resolved in endFrame)
    std::vector<GLuint>   m_freeQueries;
    int m_frames = 0;
};

// RAII scopes. `on` is captured at construction so the destructor never touches a section it didn't
// open (toggles can't change mid-scope anyway, but this keeps begin/end symmetric).
struct CpuScope {
    const char* n; bool on;
    CpuScope(const char* name, ProfLevel lvl = ProfLevel::Coarse) : n(name) {
        on = Profiler::CpuEnabled && (lvl == ProfLevel::Coarse || Profiler::DetailEnabled);
        if (on) Profiler::instance().cpuBegin(n, lvl);
    }
    ~CpuScope() { if (on) Profiler::instance().cpuEnd(n); }
};
struct GpuScope {
    const char* n; bool on;
    GpuScope(const char* name, ProfLevel lvl = ProfLevel::Coarse) : n(name) {
        on = Profiler::GpuEnabled && (lvl == ProfLevel::Coarse || Profiler::DetailEnabled);
        if (on) Profiler::instance().gpuBegin(n, lvl);
    }
    ~GpuScope() { if (on) Profiler::instance().gpuEnd(n); }
};

// Two-level concat so __LINE__ expands before the paste (unique local name per macro use).
#define PROF_CAT2(a, b) a##b
#define PROF_CAT(a, b)  PROF_CAT2(a, b)
#define PROFILE_CPU(name)        CpuScope PROF_CAT(_pc_, __LINE__)(name)
#define PROFILE_CPU_DETAIL(name) CpuScope PROF_CAT(_pc_, __LINE__)(name, ProfLevel::Detail)
#define PROFILE_GPU(name)        GpuScope PROF_CAT(_pg_, __LINE__)(name)
#define PROFILE_GPU_DETAIL(name) GpuScope PROF_CAT(_pg_, __LINE__)(name, ProfLevel::Detail)
