#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Components/ColliderShapes.h"      // shared shape geometry + manifold() + Contact
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include "Core/Engine/Physics/physics_manager.h"   // owns the body list + drives the step
#include "Components/Rendering/MeshComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

// first iteration: [phys2] N=256  detect=6.01731ms  solve=28.5125ms  total=34.5298ms  (12 threads)
// second iteration, improved detection: [phys2] N=258  detect=1.91467ms  solve=24.4167ms  total=26.3313ms  (12 threads)
// third iteration early out: [phys2] N=256  detect=2.35294ms  solve=23.6921ms  total=26.045ms  (12 threads)
// third but spread out boxes instead of pile: [phys2] N=256  detect=1.10597ms  solve=3.5679ms  total=4.67387ms  (12 threads)
// fourth iteration contact islands (spread): [phys2] N=264  detect=1.13651ms  solve=0.872342ms  total=2.00885ms  (12 threads)
// with pile: [phys2] N=256  detect=2.08472ms  solve=17.8367ms  total=19.9214ms  (12 threads)
// fifth iteration with island coloring: [phys2] N=256  detect=2.11326ms  solve=18.0347ms  total=20.148ms  (12 threads)
// but some time after the island settles: [phys2] N=256  detect=2.17994ms  solve=5.52122ms  total=7.70116ms  (12 threads)

// RigidBody 3D (v2) — the rework. Differs from OldRigidBodyComponent3D ("OldRigidBody3D") in two
// fundamental ways:
//
//   1. MERGED COLLIDER. The body owns its own collision SURFACE (a ColliderShape + layer/mask) and
//      runs its own narrowphase, so a physics object is just Transform + Mesh + RigidBody3D (no
//      separate Collider). The old ColliderComponent3D stays for HurtBox/legacy.
//
//   2. ACCUMULATED-IMPULSE PGS SOLVER. Multi-contact resolution is a proper velocity LCP solved by
//      Projected Gauss-Seidel: each contact keeps a RUNNING normal impulse lambda, clamped >= 0,
//      and only the DELTA is applied each iteration. That converges to a consistent set of contact
//      forces that sum to zero, so a resting pile actually comes to rest instead of jittering (the
//      old solver recomputed each impulse from scratch every iteration and never converged).
//      Penetration is removed by a SEPARATE split-impulse (pseudo-velocity) position pass, so fixing
//      overlap never injects energy back into the real velocity. Boxed Coulomb friction is solved in
//      the same loop, coupled to the accumulated normal impulse.
//
// This version drops sleep and the authored center of mass (CoM is the shape centroid = origin).
// The lifecycle-timer fields are PARSED but not yet acted on (behavior comes later).
//
// JSON: { "type": "RigidBody3D", "mass": 1.0, "friction": 0.5, "rollingFriction": 0.05,
//         "restitution": 0.1, "drag": 0.05, "angularDrag": 0.05, "static": false, "useGravity": true,
//         "gravity": [0,-9.81,0], "simulation": "simple"|"complex",
//         "layer": 1, "mask": 4294967295,
//         "shape": { "kind": "obb", "halfExtents": [0.5,0.5,0.5] },
//         "initialVelocity": [0,0,0], "initialAngularVelocity": [0,0,0],
//         "ttl": -1, "ttlStart": "spawn"|"contact",          // PLACED, not implemented yet
//         "freezeAfter": -1, "freezeStart": "spawn"|"contact" }
class RigidBodyComponent3D : public SpatialComponent3D {
    MeshComponent* mesh = nullptr;

public:
    enum class Simulation { Simple, Complex };
    enum class TimerStart { Spawn, Contact };

    // Per-frame state dump (awake bodies). Zero overhead when false.
    inline static bool DebugPrint = false;
    // Phase timing: averaged ms for detection vs solve vs total, printed periodically. Lets you see
    // whether the narrowphase (parallel) or the serial PGS solve dominates the physics step.
    inline static bool DebugTimings = true;

    // --- collision surface (merged collider) ---
    std::unique_ptr<ColliderShape> Shape;
    uint32_t Layer = 0x1;
    uint32_t Mask  = 0xFFFFFFFF;

    // --- dynamics ---
    float      Mass        = 1.0f;            // <= 0 or Static => infinite mass (immovable)
    float      Friction    = 0.5f;            // Coulomb coefficient — combined by geometric mean
    float      RollingFriction = 0.05f;       // rolling resistance — damps spin so rollers (spheres) stop
    float      Restitution = 0.1f;            // bounciness [0,1], only above an impact-speed threshold
    float      Drag        = 0.05f;           // linear velocity damping
    float      AngularDrag = 0.05f;           // angular velocity damping (Complex)
    bool       Static      = false;           // immovable world geometry
    bool       UseGravity  = true;
    Simulation Mode        = Simulation::Complex;   // v2 defaults to full angular dynamics
    glm::vec3  Gravity{0.0f, -9.81f, 0.0f};
    glm::vec3  Velocity{0.0f};
    glm::vec3  AngularVelocity{0.0f};

    // --- lifecycle timers (placed; behavior implemented later) ---
    float      TTL         = -1.0f;
    float      FreezeAfter = -1.0f;
    TimerStart TtlStart    = TimerStart::Spawn;
    TimerStart FreezeStart = TimerStart::Spawn;

    // --- force / impulse API ---
    void AddForce(const glm::vec3& f)            { m_forceAccumulator += f; }
    void AddImpulse(const glm::vec3& j)          { Velocity += j * m_invMass; }
    void AddTorque(const glm::vec3& t)           { m_torqueAccumulator += t; }
    void SetVelocity(const glm::vec3& v)         { Velocity = v; }
    void AddForceAtPosition(const glm::vec3& f, const glm::vec3& worldPos) {
        m_forceAccumulator  += f;
        m_torqueAccumulator += glm::cross(worldPos - worldCenter(), f);
    }
    void AddImpulseAtPoint(const glm::vec3& P, const glm::vec3& worldPos) {
        applyImpulseAtArm(P, worldPos - worldCenter());
    }

    float inverseMass() const { return m_invMass; }

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Mass        = j.value("mass",        Mass);
        Friction    = j.value("friction",    Friction);
        RollingFriction = j.value("rollingFriction", RollingFriction);
        Restitution = j.value("restitution", Restitution);
        Drag        = j.value("drag",        Drag);
        AngularDrag = j.value("angularDrag", AngularDrag);
        Static      = j.value("static",      Static);
        UseGravity  = j.value("useGravity",  UseGravity);
        const std::string sim = j.value("simulation", j.value("mode", std::string{"complex"}));
        Mode = (sim == "simple") ? Simulation::Simple : Simulation::Complex;
        Gravity         = jsonVec3(j.value("gravity",                nlohmann::json{}), Gravity);
        Velocity        = jsonVec3(j.value("initialVelocity",        nlohmann::json{}), Velocity);
        AngularVelocity = jsonVec3(j.value("initialAngularVelocity", nlohmann::json{}), AngularVelocity);

        if (j.contains("layer")) Layer = j["layer"].get<uint32_t>();
        if (j.contains("mask"))  Mask  = j["mask"].get<uint32_t>();
        parseShape(j);

        // Timer fields: parsed and stored, but not yet acted on.
        TTL         = j.value("ttl",         TTL);
        FreezeAfter = j.value("freezeAfter", FreezeAfter);
        TtlStart    = parseStart(j.value("ttlStart",    std::string{"spawn"}));
        FreezeStart = parseStart(j.value("freezeStart", std::string{"spawn"}));
    }

    void start() override {
        SpatialComponent3D::start();
        mesh = owner->getComponent<MeshComponent>();
        if (!Shape) throw MissingComponentException("RigidBodyComponent3D", "shape");

        // One authoritative transform: the body, its shape, and the mesh all follow the owner.
        this->useOwnerPosition();  this->useOwnerOrientation();
        if (mesh) { mesh->useOwnerPosition(); mesh->useOwnerOrientation(); }

        m_invMass = (Static || Mass <= 0.0f) ? 0.0f : 1.0f / Mass; //if static or mass negative = 0, else 1.0f/mass
        m_invInertiaLocal = computeInvInertiaLocal();
        PhysicsManager::instance().add(this);   // self-register (manager owns the list now)
    }

    ~RigidBodyComponent3D() override {
        PhysicsManager::instance().remove(this);
    }

    static const std::vector<RigidBodyComponent3D*>& world() { return PhysicsManager::instance().bodies(); }

    // One fixed physics slice for the NEW world. Pipeline:
    //   1. integrate velocities (gravity/forces, drag)   2. sync shapes
    //   3. narrowphase -> contact constraints             4. velocity LCP (PGS: normal + friction)
    //   5. integrate positions                            6. split-impulse position correction
    static void stepWorld(const std::vector<RigidBodyComponent3D*>& world, UniformGrid& grid, float dt) {
        const size_t n = world.size();
        if (n == 0) return;

        // Detection (O(N^2)) parallelizes at a LOW body count so it engages early; the cheap
        // per-body O(N) loops only pay for the thread fork/join at larger N. Tune to your cores.
        constexpr size_t kParDetect = 32;
        constexpr size_t kParBody   = 256;

#ifdef _OPENMP
        // Read the CPU's hardware thread count from OpenMP ONCE and use all of them for the parallel
        // regions: omp_get_num_procs() is the logical CPU thread count (e.g. 12), and
        // omp_set_num_threads() makes every parallel-for below default to that team size (overriding
        // any environment default).
        static const int kThreads = []{
            const int t = omp_get_num_procs();
            omp_set_num_threads(t);
            return t;
        }();

        // Announce when the parallel narrowphase switches on/off — only on the transition (crossing
        // kParDetect), so it isn't spammy.
        {
            static bool s_wasParallel = false;
            const bool parallel = n > kParDetect;
            if (parallel != s_wasParallel) {
                std::cout << "[phys2] narrowphase multithreading " << (parallel ? "ON" : "OFF")
                          << " (bodies=" << n << ", CPU threads=" << kThreads << ")\n";
                s_wasParallel = parallel;
            }
        }
#endif

        const auto _tStart = std::chrono::high_resolution_clock::now();

        // 1. Integrate velocities only (pose advances in step 5). Reset per-step solver state.
        //    Per-body independent → parallel.
        #pragma omp parallel for schedule(static) if(n > kParBody)
        for (size_t i = 0; i < n; ++i) {
            auto* b = world[i];
            b->m_biasVel = glm::vec3(0.0f);
            b->m_biasAngVel = glm::vec3(0.0f);
            if (b->m_invMass == 0.0f) { b->m_forceAccumulator = glm::vec3(0.0f); b->m_torqueAccumulator = glm::vec3(0.0f); continue; }
            b->Velocity += (b->m_forceAccumulator * b->m_invMass + (b->UseGravity ? b->Gravity : glm::vec3(0.0f))) * dt;
            b->Velocity *= glm::clamp(1.0f - b->Drag * dt, 0.0f, 1.0f);
            b->m_forceAccumulator = glm::vec3(0.0f);
            // World inverse inertia from the current orientation (used by the solver this step).
            const glm::mat3 R = glm::mat3_cast(b->orientation());
            b->m_invInertiaWorld = R * b->m_invInertiaLocal * glm::transpose(R);
            if (b->Mode == Simulation::Complex) {
                b->AngularVelocity += (b->m_invInertiaWorld * b->m_torqueAccumulator) * dt;
                b->AngularVelocity *= glm::clamp(1.0f - b->AngularDrag * dt, 0.0f, 1.0f);
            }
            b->m_torqueAccumulator = glm::vec3(0.0f);
        }

        // 2. Sync shape orientation (OBB axes / plane normal). Per-body independent → parallel.
        #pragma omp parallel for schedule(static) if(n > kParBody)
        for (size_t i = 0; i < n; ++i)
            if (world[i]->Shape) world[i]->Shape->setOrientation(world[i]->orientation());

        // 3a. Broadphase build — SERIAL scatter, O(N). Compute each body's world AABB and bin it into
        //     the grid. Infinite planes go to the unbounded list (a plane can't be celled — its AABB
        //     spans everything); out-of-range or shapeless bodies are skipped. The AABB is cached on
        //     the body so the parallel pass below doesn't recompute it. This replaces the old O(N^2)
        //     all-pairs sweep: bodies that share no cell are never tested.
        grid.clear();
        for (size_t i = 0; i < n; ++i) {
            auto* A = world[i];
            A->m_bodyIndex = int(i);    // for island union-find later in the step
            A->m_inGrid = false;
            if (!A->Shape) { A->m_aabbInfinite = false; continue; }
            const AABBox box = A->Shape->worldAABB(A->position(), A->scale());
            A->m_aabbMin = box.min; A->m_aabbMax = box.max; A->m_aabbInfinite = box.infinite;
            if (box.infinite) { grid.unbounded.push_back(int(i)); continue; }   // planes: tested vs every finite body
            if (!grid.inRange(A->position())) continue;                         // range cull (broadphase only for now)
            grid.insert(int(i), box.min, box.max);
            A->m_inGrid = true;
        }

        // 3b. Narrowphase — PARALLEL. Each finite, in-grid body queries its cells for the neighbours it
        //     OWNS as the lower index (j>i, so every finite–finite pair is built exactly once and
        //     threads never write shared state), then tests against the unbounded planes. Writes only
        //     its OWN row; the serial concat below flattens in body order (deterministic).
        #pragma omp parallel for schedule(guided) if(n > kParDetect)
        for (size_t i = 0; i < n; ++i) {
            RigidBodyComponent3D* A = world[i];
            A->m_row.clear();
            if (!A->Shape || A->m_aabbInfinite || !A->m_inGrid) continue;   // planes & culled bodies drive nothing

            thread_local std::vector<int> cand;
            cand.clear();
            grid.query(A->m_aabbMin, A->m_aabbMax, cand);
            std::sort(cand.begin(), cand.end());
            cand.erase(std::unique(cand.begin(), cand.end()), cand.end());   // dedup multi-cell neighbours

            for (int j : cand) if (size_t(j) > i) narrowPair(A, world[j], A->m_row);   // finite–finite, once
            for (int p : grid.unbounded)          narrowPair(A, world[p], A->m_row);   // finite–plane (A drives)
        }
        static std::vector<ContactConstraint> constraints;
        constraints.clear();
        for (size_t i = 0; i < n; ++i) {
            auto& row = world[i]->m_row;
            constraints.insert(constraints.end(), row.begin(), row.end());
        }
        const auto _tDetect = std::chrono::high_resolution_clock::now();

        // --- ISLAND BUILD — union-find over the contacts. Two dynamic bodies linked by a contact
        //     (directly or transitively) form one island that MUST be solved together. STATICS are
        //     never union nodes: unioning through them would fuse every pile resting on the same ground
        //     into one giant island. A static just rides along in whichever islands touch it, and since
        //     impulses to a static are no-ops (skipped in applyImpulseAtArm / solvePosition), the same
        //     static appearing in several islands solved on different threads is race-free.
        static std::vector<int> ufParent, ufRank;
        ufParent.resize(n); ufRank.assign(n, 0);
        for (size_t i = 0; i < n; ++i) ufParent[i] = int(i);
        for (auto& c : constraints)
            if (c.A->m_invMass != 0.0f && c.B->m_invMass != 0.0f)          // dynamic–dynamic edge only
                ufUnite(ufParent, ufRank, c.A->m_bodyIndex, c.B->m_bodyIndex);
        for (auto& c : constraints) {                                      // tag each contact with its island root (the dynamic side)
            const int dyn = (c.A->m_invMass != 0.0f) ? c.A->m_bodyIndex : c.B->m_bodyIndex;
            c.island = ufFind(ufParent, dyn);
        }
        // Group contacts into contiguous per-island ranges; stable_sort keeps body order within island
        // (so the solve order — and thus the result — stays deterministic regardless of thread scheduling).
        std::stable_sort(constraints.begin(), constraints.end(),
                         [](const ContactConstraint& a, const ContactConstraint& b) { return a.island < b.island; });
        // Islands with more than this many contacts are too big to leave on one thread (a pile is one
        // island), so we graph-COLOUR them and solve colour-batches in parallel. Smaller islands stay
        // whole and parallelise across each other instead. Tune to where colouring beats serial.
        constexpr size_t kColorThreshold = 512;
        static std::vector<Island> islands;
        islands.clear();
        for (size_t s = 0; s < constraints.size(); ) {
            size_t e = s + 1;
            while (e < constraints.size() && constraints[e].island == constraints[s].island) ++e;
            const bool colored = (e - s > kColorThreshold) && colorIsland(constraints, s, e);
            islands.push_back({ s, e, colored });
            s = e;
        }

        // 3b+4. Per-island velocity solve (warm start → PGS → rolling), in two phases so OpenMP regions
        //       never nest:
        //   Phase A — BIG coloured islands: each runs alone but uses ALL threads internally, parallel
        //             over colour batches (a colour's contacts share no body → race-free). No early-out;
        //             a big pile needs every iteration.
        //   Phase B — the rest: parallel-for ACROSS islands, each solved serially on its own thread
        //             (symmetric sweep + early-out, the cheap path for settled clusters).
        for (auto& isl : islands)
            if (isl.colored) solveIslandColoredVelocity(constraints, isl.begin, isl.end);
        #pragma omp parallel for schedule(dynamic) if(islands.size() > 1)
        for (size_t i = 0; i < islands.size(); ++i)
            if (!islands[i].colored) solveIslandVelocity(constraints, islands[i].begin, islands[i].end);

        // Store the converged impulses for next frame's warm start (fresh map drops stale contacts).
        auto& cache = impulseCache();
        cache.clear();
        for (auto& c : constraints) cache[c.key] = { c.lambdaN, c.lambdaT1, c.lambdaT2 };

        // 5. Integrate positions from the (now consistent) velocities. Per-body → parallel.
        #pragma omp parallel for schedule(static) if(n > kParBody)
        for (size_t i = 0; i < n; ++i) {
            auto* b = world[i];
            if (b->m_invMass == 0.0f) continue;   // skip static
            auto* tf = b->owner->getComponent<TransformComponent3D>();
            tf->Position += b->Velocity * dt;
            if (b->Mode == Simulation::Complex) integrateOrientation(tf->Orientation, b->AngularVelocity, dt);
        }

        // 6. Split-impulse position correction — pushes out the detected overlap using PSEUDO
        //    velocities only, so the real velocity (and thus the resting state) is never disturbed.
        //    PARALLEL over islands (disjoint bodies, same as the velocity solve); the per-body bias
        //    integration below is also parallel.
        for (auto& isl : islands)
            if (isl.colored) solveIslandColoredPosition(constraints, isl.begin, isl.end, dt);
        #pragma omp parallel for schedule(dynamic) if(islands.size() > 1)
        for (size_t i = 0; i < islands.size(); ++i)
            if (!islands[i].colored) solveIslandPosition(constraints, islands[i].begin, islands[i].end, dt);
        #pragma omp parallel for schedule(static) if(n > kParBody)
        for (size_t i = 0; i < n; ++i) {
            auto* b = world[i];
            if (b->m_invMass == 0.0f) continue;
            if (b->m_biasVel == glm::vec3(0.0f) && b->m_biasAngVel == glm::vec3(0.0f)) continue;
            auto* tf = b->owner->getComponent<TransformComponent3D>();
            tf->Position += b->m_biasVel * dt;
            if (b->Mode == Simulation::Complex) integrateOrientation(tf->Orientation, b->m_biasAngVel, dt);
        }

        if (DebugTimings) {
            const auto _tEnd = std::chrono::high_resolution_clock::now();
            using ms = std::chrono::duration<double, std::milli>;
            static double accDetect = 0, accSolve = 0, accTotal = 0; static int frames = 0;
            accDetect += ms(_tDetect - _tStart).count();        // integrate + syncShape + narrowphase
            accSolve  += ms(_tEnd    - _tDetect).count();       // warm-start + velocity/position PGS
            accTotal  += ms(_tEnd    - _tStart).count();
            if (++frames >= 60) {
                std::cout << "[phys2] N=" << n
                          << "  detect=" << (accDetect / frames) << "ms"
                          << "  solve="  << (accSolve  / frames) << "ms"
                          << "  total="  << (accTotal  / frames) << "ms"
#ifdef _OPENMP
                          << "  (" << kThreads << " threads)"
#endif
                          << "\n";
                accDetect = accSolve = accTotal = 0.0; frames = 0;
            }
        }

        if (DebugPrint) {
            static int tick = 0;
            if (tick++ % 6 == 0)
                for (auto* b : world) {
                    if (b->m_invMass == 0.0f) continue;
                    // Sum the support impulse + contact count for this body — lambdaN is what budgets
                    // BOTH friction (mu*lambdaN) and rolling resistance (muRoll*lambdaN), so if it's ~0
                    // those params can't bite. For a roller, |w|*r ~= |v| means it's actually rolling.
                    float lamN = 0.0f; int nc = 0;
                    for (const auto& c : constraints) if (c.A == b || c.B == b) { lamN += c.lambdaN; ++nc; }
                    const glm::vec3 p = b->position();
                    std::cout << "[phys2] " << b->owner->Name
                              << " pos(" << p.x << ", " << p.y << ", " << p.z << ")"
                              << " |v|=" << glm::length(b->Velocity)
                              << " |w|=" << glm::length(b->AngularVelocity)
                              << " nc=" << nc << " lambdaN=" << lamN << "\n";
                }
        }
    }

private:
    glm::vec3 m_forceAccumulator{0.0f};
    glm::vec3 m_torqueAccumulator{0.0f};
    float     m_invMass = 1.0f;
    glm::mat3 m_invInertiaLocal{0.0f};
    glm::mat3 m_invInertiaWorld{0.0f};
    glm::vec3 m_biasVel{0.0f};       // split-impulse pseudo linear velocity (position pass only)
    glm::vec3 m_biasAngVel{0.0f};    // split-impulse pseudo angular velocity

    // Broadphase scratch (filled by stepWorld's grid build, read by the narrowphase pass).
    glm::vec3 m_aabbMin{0.0f}, m_aabbMax{0.0f};
    bool      m_aabbInfinite = false;   // shape has no finite AABB (plane) → in the unbounded list, not the grid
    bool      m_inGrid       = false;   // finite + in range → inserted into the grid this step
    int       m_bodyIndex    = -1;      // this body's index in the world list (key for island union-find)
    uint64_t  m_colorMask    = 0;       // scratch: colours already used at this body during in-island graph colouring

    // CoM is the shape centroid (origin), so the world center of mass is just the position.
    glm::vec3 worldCenter() const { return position(); }

    static TimerStart parseStart(const std::string& s) {
        return s == "contact" ? TimerStart::Contact : TimerStart::Spawn;
    }

    // Parse the "shape" block — same vocabulary as ColliderComponent3D.
    void parseShape(const nlohmann::json& j) {
        if (!j.contains("shape")) return;
        const nlohmann::json& s = j["shape"];
        const std::string kind = s.value("kind", std::string{"box"});
        if (kind == "box") {
            Shape = std::make_unique<BoxShape>(s.contains("halfExtents") ? jsonVec3(s["halfExtents"], glm::vec3(0.5f)) : glm::vec3(0.5f));
        } else if (kind == "obb") {
            Shape = std::make_unique<OBBShape>(s.contains("halfExtents") ? jsonVec3(s["halfExtents"], glm::vec3(0.5f)) : glm::vec3(0.5f));
        } else if (kind == "sphere") {
            Shape = std::make_unique<SphereShape>(s.value("radius", 0.5f));
        } else if (kind == "ball") {
            Shape = std::make_unique<OrientedSphereShape>(s.value("radius", 0.5f));
        } else if (kind == "plane") {
            glm::vec3 n = s.contains("normal") ? jsonVec3(s["normal"], glm::vec3(0, 1, 0)) : glm::vec3(0, 1, 0);
            const bool infinite = s.value("infinite", true);
            const bool oneSided = s.value("oneSided", true);
            glm::vec2 he{5.0f, 5.0f};
            if (s.contains("halfExtents") && s["halfExtents"].is_array() && s["halfExtents"].size() >= 2)
                he = { s["halfExtents"][0].get<float>(), s["halfExtents"][1].get<float>() };
            Shape = std::make_unique<PlaneShape>(n, infinite, he, oneSided);
        } else {
            std::cerr << "[RigidBodyComponent3D] unknown shape kind: " << kind << "\n";
        }
    }

    // Inverse inertia tensor in LOCAL space from shape x mass. Zero for Simple/Static/plane.
    glm::mat3 computeInvInertiaLocal() const {
        if (Mode != Simulation::Complex || m_invMass == 0.0f || !Shape) return glm::mat3(0.0f);
        const glm::vec3 sc = scale();
        const float m = Mass;
        switch (Shape->kind()) {
            case ShapeKind::Box:
            case ShapeKind::OBB: {
                const glm::vec3 he = (Shape->kind() == ShapeKind::Box)
                    ? static_cast<BoxShape*>(Shape.get())->HalfExtents
                    : static_cast<OBBShape*>(Shape.get())->HalfExtents;
                const glm::vec3 w = 2.0f * he * sc;
                const float ix = (1.0f/12.0f) * m * (w.y*w.y + w.z*w.z);
                const float iy = (1.0f/12.0f) * m * (w.x*w.x + w.z*w.z);
                const float iz = (1.0f/12.0f) * m * (w.x*w.x + w.y*w.y);
                return glm::mat3(glm::vec3(1.0f/ix, 0, 0), glm::vec3(0, 1.0f/iy, 0), glm::vec3(0, 0, 1.0f/iz));
            }
            case ShapeKind::Sphere:
            case ShapeKind::Ball: {
                const float r = static_cast<SphereShape*>(Shape.get())->Radius * sc.x;
                const float i = (2.0f/5.0f) * m * r * r;
                return glm::mat3(i > 0.0f ? 1.0f/i : 0.0f);
            }
            default: return glm::mat3(0.0f);
        }
    }

    void applyImpulseAtArm(const glm::vec3& P, const glm::vec3& r) {
        if (m_invMass == 0.0f) return;   // static: immovable, and skipping the no-op write keeps the parallel island solve race-free
        Velocity += P * m_invMass;
        AngularVelocity += m_invInertiaWorld * glm::cross(r, P);
    }

    // Rolling resistance: a bounded counter-impulse opposing spin so a roller (sphere) slows and
    // stops — sliding friction alone can't, since a pure roll has zero slip at the contact point.
    // `budget` (= muRoll * normalImpulse) is scaled by the lever arm and never overshoots past zero.
    void applyRollingResistance(const glm::vec3& r, float budget) {
        const float wl = glm::length(AngularVelocity);
        if (wl < 1e-6f) return;
        const glm::vec3 axis = AngularVelocity / wl;
        const float invEff = glm::dot(axis, m_invInertiaWorld * axis);   // 1 / I about the spin axis
        if (invEff < 1e-8f) return;
        const float stopImpulse = wl / invEff;                           // angular impulse to fully stop
        const float maxImpulse  = budget * glm::length(r);
        AngularVelocity -= axis * (std::min(stopImpulse, maxImpulse) * invEff);
    }

    static glm::vec3 pointVel(const RigidBodyComponent3D* b, const glm::vec3& r) {
        return b->Velocity + glm::cross(b->AngularVelocity, r);
    }
    static glm::vec3 biasPointVel(const RigidBodyComponent3D* b, const glm::vec3& r) {
        return b->m_biasVel + glm::cross(b->m_biasAngVel, r);
    }

    static void integrateOrientation(glm::quat& q, const glm::vec3& w, float dt) {
        const glm::quat spin(0.0f, w.x, w.y, w.z);
        q = glm::normalize(q + 0.5f * spin * q * dt);
    }

    // ---- contact constraint (one per manifold point) ----
    struct ContactConstraint {
        RigidBodyComponent3D* A;
        RigidBodyComponent3D* B;
        glm::vec3 n, t1, t2;          // normal (A->B) + tangent basis
        glm::vec3 rA, rB;             // arms from each center to the contact point
        float normalMass = 0.0f;
        float tanMass1 = 0.0f, tanMass2 = 0.0f;
        float restitutionBias = 0.0f; // target normal velocity from restitution (<=0)
        float penetration = 0.0f;
        float lambdaN = 0.0f;         // accumulated normal impulse (>= 0)
        float lambdaT1 = 0.0f, lambdaT2 = 0.0f;
        float lambdaPos = 0.0f;       // accumulated position-correction impulse (>= 0)
        uint64_t key = 0;             // stable id (bodyA, bodyB, manifold slot) for warm-starting
        int      island = -1;         // island root (union-find) — contacts are grouped by this for the parallel solve
        int      color  = 0;          // graph-colouring class within a big island (no two same-colour contacts share a body)
    };

    // Per-body narrowphase scratch: the parallel detection loop fills each body's OWN row (the
    // pairs it owns as the lower index), so threads never write shared state. Concatenated into the
    // flat constraint list — in body order, so the solve order stays deterministic. Cleared (not
    // freed) each step to reuse capacity.
    std::vector<ContactConstraint> m_row;

    // Warm-start cache: last frame's accumulated impulses, keyed per contact. Re-applying them so
    // the solver starts near the solution makes lambdaN reach the true support force immediately,
    // which is what gives friction (mu * lambdaN) a real budget on resting stacks (without it the
    // velocity normal impulse stays tiny — split-impulse does the holding — and stacks slide).
    struct CachedImpulse { float n = 0.0f, t1 = 0.0f, t2 = 0.0f; };
    static std::unordered_map<uint64_t, CachedImpulse>& impulseCache() {
        static std::unordered_map<uint64_t, CachedImpulse> m;
        return m;
    }
    static uint64_t contactKey(const RigidBodyComponent3D* A, const RigidBodyComponent3D* B, int slot) {
        const uint64_t a = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(A));
        const uint64_t b = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(B));
        return (a * 0x9E3779B97F4A7C15ull) ^ (b * 0xC2B2AE3D27D4EB4Full) ^ (uint64_t(slot) * 0x165667B19E3779F9ull);
    }

    // Effective mass along a unit axis L at this contact (the old kN formula).
    static float effectiveMass(const ContactConstraint& c, const glm::vec3& L) {
        const glm::vec3 rnA = glm::cross(c.rA, L), rnB = glm::cross(c.rB, L);
        const float k = c.A->m_invMass + c.B->m_invMass
                      + glm::dot(L, glm::cross(c.A->m_invInertiaWorld * rnA, c.rA))
                      + glm::dot(L, glm::cross(c.B->m_invInertiaWorld * rnB, c.rB));
        return k > 0.0f ? 1.0f / k : 0.0f;
    }

    static void buildConstraint(RigidBodyComponent3D* A, RigidBodyComponent3D* B,
                                const Contact& pt, int slot, std::vector<ContactConstraint>& out) {
        ContactConstraint c;
        c.A = A; c.B = B;
        c.n = glm::normalize(pt.normal);
        c.penetration = pt.penetration;
        c.rA = pt.point - A->worldCenter();
        c.rB = pt.point - B->worldCenter();

        // Tangent basis perpendicular to n.
        if (std::abs(c.n.x) >= 0.57735f) c.t1 = glm::normalize(glm::vec3(c.n.y, -c.n.x, 0.0f));
        else                             c.t1 = glm::normalize(glm::vec3(0.0f, c.n.z, -c.n.y));
        c.t2 = glm::cross(c.n, c.t1);

        c.normalMass = effectiveMass(c, c.n);
        c.tanMass1   = effectiveMass(c, c.t1);
        c.tanMass2   = effectiveMass(c, c.t2);

        // Restitution bias: only for real impacts (below a small approach speed, treat as e=0 so
        // resting contacts don't bounce). vn < 0 == approaching.
        const float vn = glm::dot(pointVel(B, c.rB) - pointVel(A, c.rA), c.n);
        constexpr float kRestitutionThreshold = 0.5f;
        const float e = std::sqrt(std::max(0.0f, A->Restitution * B->Restitution));
        c.restitutionBias = (vn < -kRestitutionThreshold) ? e * vn : 0.0f;

        // Warm start: seed the accumulated impulses from last frame's solution for this contact.
        c.key = contactKey(A, B, slot);
        auto it = impulseCache().find(c.key);
        if (it != impulseCache().end()) {
            c.lambdaN  = it->second.n;
            c.lambdaT1 = it->second.t1;
            c.lambdaT2 = it->second.t2;
        }

        out.push_back(c);
    }

    // One candidate pair → contacts appended to `row`. Mirrors the old inner-loop body: skip
    // static-static and non-overlapping layers, let the DYNAMIC body drive the manifold (so
    // box-vs-plane uses the box's corner manifold), append a constraint per contact point. The
    // caller guarantees each pair is passed exactly once. Defined after ContactConstraint/buildConstraint
    // so the std::vector<ContactConstraint>& parameter type is complete.
    static void narrowPair(RigidBodyComponent3D* A, RigidBodyComponent3D* B, std::vector<ContactConstraint>& row) {
        if (!B->Shape) return;
        if (A->m_invMass == 0.0f && B->m_invMass == 0.0f) return;       // static vs static
        if (!layerMaskOverlap(A->Layer, A->Mask, B->Layer, B->Mask)) return;
        RigidBodyComponent3D* caller = (A->m_invMass != 0.0f) ? A : B;
        RigidBodyComponent3D* other  = (caller == A) ? B : A;
        Contact pts[8];
        const int nc = caller->Shape->manifold(*other->Shape,
                                               caller->position(), caller->scale(),
                                               other->position(),  other->scale(), pts, 8);
        for (int c = 0; c < nc; ++c) buildConstraint(caller, other, pts[c], c, row);
    }

    // Normal constraint: drive (vn + restitutionBias) -> 0, with the accumulated impulse clamped >= 0.
    // Returns the magnitude of the impulse actually applied (after the clamp) so the solve loop can
    // detect convergence and early-out.
    static float solveNormal(ContactConstraint& c) {
        const float vn = glm::dot(pointVel(c.B, c.rB) - pointVel(c.A, c.rA), c.n);
        const float dl = -(vn + c.restitutionBias) * c.normalMass;
        const float old = c.lambdaN;
        c.lambdaN = std::max(0.0f, old + dl);
        const float applied = c.lambdaN - old;          // real change after clamp (n is unit → this is |P|)
        const glm::vec3 P = applied * c.n;
        c.A->applyImpulseAtArm(-P, c.rA);
        c.B->applyImpulseAtArm( P, c.rB);
        return std::abs(applied);
    }

    // Coulomb friction on both tangents, clamped to the friction CONE: the COMBINED tangent impulse
    // magnitude is limited to mu * lambdaN (a disk), not each tangent independently (a square). The
    // square ("box") version over-applies friction along the diagonal, so a diagonally-sliding body
    // gets dragged toward a tangent axis — on a plane the tangents are world X/Z, so boxes visibly
    // "snap" to those axes. The cone is isotropic: friction opposes the actual slide direction.
    // Returns the magnitude of the tangent impulse delta applied this call (for the solve early-out).
    static float solveFriction(ContactConstraint& c) {
        const float mu   = std::sqrt(std::max(0.0f, c.A->Friction * c.B->Friction));
        const float maxF = mu * c.lambdaN;
        const glm::vec3 vrel = pointVel(c.B, c.rB) - pointVel(c.A, c.rA);

        float newT1 = c.lambdaT1 - glm::dot(vrel, c.t1) * c.tanMass1;
        float newT2 = c.lambdaT2 - glm::dot(vrel, c.t2) * c.tanMass2;

        // Clamp the (t1, t2) impulse vector into the disk of radius maxF.
        const float mag = std::sqrt(newT1 * newT1 + newT2 * newT2);
        if (mag > maxF && mag > 1e-12f) {
            const float s = maxF / mag;
            newT1 *= s; newT2 *= s;
        }

        const float dT1 = newT1 - c.lambdaT1, dT2 = newT2 - c.lambdaT2;   // t1,t2 orthonormal → |P| = hypot(dT1,dT2)
        const glm::vec3 P = dT1 * c.t1 + dT2 * c.t2;
        c.lambdaT1 = newT1; c.lambdaT2 = newT2;
        c.A->applyImpulseAtArm(-P, c.rA);
        c.B->applyImpulseAtArm( P, c.rB);
        return std::sqrt(dT1 * dT1 + dT2 * dT2);
    }

    // Split-impulse: push the pair apart along the normal using PSEUDO velocities (never the real
    // velocity), so removing penetration adds no energy. beta scales how fast overlap is removed.
    static void solvePosition(ContactConstraint& c, float dt) {
        constexpr float kBeta = 0.2f, kSlop = 0.005f;
        const float target = (kBeta / dt) * std::max(c.penetration - kSlop, 0.0f);   // desired separating speed
        if (target <= 0.0f) return;
        const float vn = glm::dot(biasPointVel(c.B, c.rB) - biasPointVel(c.A, c.rA), c.n);
        const float dl = (target - vn) * c.normalMass;
        const float old = c.lambdaPos;
        c.lambdaPos = std::max(0.0f, old + dl);
        const glm::vec3 P = (c.lambdaPos - old) * c.n;
        // Apply to the bias (pseudo) velocities only. Skip statics: no-op writes, and skipping them
        // keeps the parallel per-island position solve race-free on a shared static.
        if (c.A->m_invMass != 0.0f) {
            c.A->m_biasVel    -= P * c.A->m_invMass;
            c.A->m_biasAngVel -= c.A->m_invInertiaWorld * glm::cross(c.rA, P);
        }
        if (c.B->m_invMass != 0.0f) {
            c.B->m_biasVel    += P * c.B->m_invMass;
            c.B->m_biasAngVel += c.B->m_invInertiaWorld * glm::cross(c.rB, P);
        }
    }

    // ---- island solve helpers (one island = a body-disjoint contact range, safe on its own thread) ----

    // Union-find over body indices: find with path-halving, union by rank.
    static int ufFind(std::vector<int>& parent, int i) {
        while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
        return i;
    }
    static void ufUnite(std::vector<int>& parent, std::vector<int>& rnk, int a, int b) {
        a = ufFind(parent, a); b = ufFind(parent, b);
        if (a == b) return;
        if (rnk[a] < rnk[b]) std::swap(a, b);
        parent[b] = a;
        if (rnk[a] == rnk[b]) ++rnk[a];
    }

    // Solve one island's contacts cs[b..e): warm start → velocity PGS (symmetric sweep + early-out) →
    // rolling resistance. Body-disjoint from every other island, so this runs lock-free on its thread.
    static void solveIslandVelocity(std::vector<ContactConstraint>& cs, size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {                              // warm start (mutates this island's bodies)
            auto& c = cs[i];
            const glm::vec3 P = c.lambdaN * c.n + c.lambdaT1 * c.t1 + c.lambdaT2 * c.t2;
            c.A->applyImpulseAtArm(-P, c.rA);
            c.B->applyImpulseAtArm( P, c.rB);
        }
        constexpr int   kVelIterations = 12;
        constexpr float kVelEpsilon    = 1e-5f;
        const size_t    m = e - b;
        for (int it = 0; it < kVelIterations; ++it) {
            float maxDelta = 0.0f;
            const bool forward = (it & 1) == 0;                        // symmetric sweep
            for (size_t k = 0; k < m; ++k) {
                auto& c = cs[b + (forward ? k : (m - 1 - k))];
                maxDelta = std::max(maxDelta, solveNormal(c));
                maxDelta = std::max(maxDelta, solveFriction(c));
            }
            if (maxDelta < kVelEpsilon) break;                         // converged → stop early
        }
        for (size_t i = b; i < e; ++i) {                              // rolling resistance
            auto& c = cs[i];
            const float muRoll = 0.5f * (c.A->RollingFriction + c.B->RollingFriction);
            if (muRoll <= 0.0f) continue;
            const float budget = muRoll * c.lambdaN;
            c.A->applyRollingResistance(c.rA, budget);
            c.B->applyRollingResistance(c.rB, budget);
        }
    }

    // Solve one island's split-impulse position correction.
    static void solveIslandPosition(std::vector<ContactConstraint>& cs, size_t b, size_t e, float dt) {
        constexpr int kPosIterations = 4;
        for (int it = 0; it < kPosIterations; ++it)
            for (size_t i = b; i < e; ++i) solvePosition(cs[i], dt);
    }

    // ---- in-island graph colouring (parallelises a single big island across threads) ----

    struct Island { size_t begin, end; bool colored; };

    // Greedy-colour the contacts of one island in place: each contact gets the lowest colour not used
    // at EITHER of its two bodies, so no two same-colour contacts share a body → a colour can be solved
    // fully in parallel. Then stable_sort by colour so each colour is a contiguous range. Returns false
    // (→ caller solves the island serially) only if a body would need ≥ 64 colours — pathological, never
    // for box piles (a box has well under 64 contact points).
    static bool colorIsland(std::vector<ContactConstraint>& cs, size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) { cs[i].A->m_colorMask = 0; cs[i].B->m_colorMask = 0; }
        for (size_t i = b; i < e; ++i) {
            auto& c = cs[i];
            const uint64_t used = c.A->m_colorMask | c.B->m_colorMask;
            if (~used == 0ull) return false;                       // 64 colours exhausted at a shared body
            const int color = __builtin_ctzll(~used);              // lowest free colour
            c.A->m_colorMask |= (1ull << color);
            c.B->m_colorMask |= (1ull << color);
            c.color = color;
        }
        std::stable_sort(cs.begin() + b, cs.begin() + e,
                         [](const ContactConstraint& a, const ContactConstraint& d) { return a.color < d.color; });
        return true;
    }

    // Find the contiguous colour-group ranges within a colour-sorted island [b,e).
    static void colorGroups(const std::vector<ContactConstraint>& cs, size_t b, size_t e,
                            std::vector<std::pair<size_t, size_t>>& out) {
        out.clear();
        for (size_t s = b; s < e; ) {
            size_t g = s + 1;
            while (g < e && cs[g].color == cs[s].color) ++g;
            out.push_back({ s, g });
            s = g;
        }
    }

    // Big-island velocity solve: warm start → kVel velocity iterations → rolling, each pass run
    // colour-by-colour with a colour's contacts solved in parallel (disjoint bodies → race-free).
    // The implicit barrier at each omp-for end sequences the colours; no early-out (a big pile needs
    // every iteration). Called from the main thread, so its parallel regions are top-level, not nested.
    static void solveIslandColoredVelocity(std::vector<ContactConstraint>& cs, size_t b, size_t e) {
        std::vector<std::pair<size_t, size_t>> groups;
        colorGroups(cs, b, e, groups);

        for (auto& gr : groups) {                                  // warm start (colours sequential; a body recurs across colours)
            #pragma omp parallel for schedule(static)
            for (size_t i = gr.first; i < gr.second; ++i) {
                auto& c = cs[i];
                const glm::vec3 P = c.lambdaN * c.n + c.lambdaT1 * c.t1 + c.lambdaT2 * c.t2;
                c.A->applyImpulseAtArm(-P, c.rA);
                c.B->applyImpulseAtArm( P, c.rB);
            }
        }
        constexpr int kVelIterations = 12;
        for (int it = 0; it < kVelIterations; ++it)
            for (auto& gr : groups) {
                #pragma omp parallel for schedule(static)
                for (size_t i = gr.first; i < gr.second; ++i) { solveNormal(cs[i]); solveFriction(cs[i]); }
            }
        for (auto& gr : groups) {                                  // rolling resistance
            #pragma omp parallel for schedule(static)
            for (size_t i = gr.first; i < gr.second; ++i) {
                auto& c = cs[i];
                const float muRoll = 0.5f * (c.A->RollingFriction + c.B->RollingFriction);
                if (muRoll <= 0.0f) continue;
                const float budget = muRoll * c.lambdaN;
                c.A->applyRollingResistance(c.rA, budget);
                c.B->applyRollingResistance(c.rB, budget);
            }
        }
    }

    // Big-island split-impulse position correction, colour-batched in parallel like the velocity solve.
    static void solveIslandColoredPosition(std::vector<ContactConstraint>& cs, size_t b, size_t e, float dt) {
        std::vector<std::pair<size_t, size_t>> groups;
        colorGroups(cs, b, e, groups);
        constexpr int kPosIterations = 4;
        for (int it = 0; it < kPosIterations; ++it)
            for (auto& gr : groups) {
                #pragma omp parallel for schedule(static)
                for (size_t i = gr.first; i < gr.second; ++i) solvePosition(cs[i], dt);
            }
    }

};

// Defined here (not in physics_manager.h) because it needs the COMPLETE RigidBodyComponent3D type;
// the manager header only forward-declares it to avoid an include cycle. The manager owns the list,
// the body owns the step logic — step() just hands its list to the (still-static) solver.
inline void PhysicsManager::step(float dt) {
    RigidBodyComponent3D::stepWorld(m_bodies, m_grid, dt);
}

REGISTER_COMPONENT(RigidBodyComponent3D, "RigidBody3D")
