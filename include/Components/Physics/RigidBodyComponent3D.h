#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Components/ColliderShapes.h"      // shared shape geometry + manifold() + Contact
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include "Components/Rendering/MeshComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

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
// JSON: { "type": "RigidBody3D", "mass": 1.0, "friction": 0.5, "restitution": 0.1,
//         "drag": 0.05, "angularDrag": 0.05, "static": false, "useGravity": true,
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

    // --- collision surface (merged collider) ---
    std::unique_ptr<ColliderShape> Shape;
    uint32_t Layer = 0x1;
    uint32_t Mask  = 0xFFFFFFFF;

    // --- dynamics ---
    float      Mass        = 1.0f;            // <= 0 or Static => infinite mass (immovable)
    float      Friction    = 0.5f;            // Coulomb coefficient — combined by geometric mean
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

        m_invMass = (Static || Mass <= 0.0f) ? 0.0f : 1.0f / Mass;
        m_invInertiaLocal = computeInvInertiaLocal();
        bodies().push_back(this);
    }

    ~RigidBodyComponent3D() override {
        auto& list = bodies();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    static const std::vector<RigidBodyComponent3D*>& world() { return bodies(); }

    // One fixed physics slice for the NEW world. Pipeline:
    //   1. integrate velocities (gravity/forces, drag)   2. sync shapes
    //   3. narrowphase -> contact constraints             4. velocity LCP (PGS: normal + friction)
    //   5. integrate positions                            6. split-impulse position correction
    static void stepWorld(float dt) {
        auto& world = bodies();
        if (world.empty()) return;

        // 1. Integrate velocities only (pose advances in step 5). Reset per-step solver state.
        for (auto* b : world) {
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

        // 2. Sync shape orientation (OBB axes / plane normal).
        for (auto* b : world) if (b->Shape) b->Shape->setOrientation(b->orientation());

        // 3. Narrowphase — every unordered pair once. Build the flat constraint list.
        static std::vector<ContactConstraint> constraints;
        constraints.clear();
        for (size_t i = 0; i < world.size(); ++i) {
            RigidBodyComponent3D* A = world[i];
            if (!A->Shape) continue;
            for (size_t k = i + 1; k < world.size(); ++k) {
                RigidBodyComponent3D* B = world[k];
                if (!B->Shape) continue;
                if (A->m_invMass == 0.0f && B->m_invMass == 0.0f) continue;       // static vs static
                if (!layerMaskOverlap(A->Layer, A->Mask, B->Layer, B->Mask)) continue;

                // The DYNAMIC body drives the manifold so a box-vs-plane pair takes the box's
                // per-corner manifold (not the plane's single-point fallback). Normal stays caller->other.
                RigidBodyComponent3D* caller = (A->m_invMass != 0.0f) ? A : B;
                RigidBodyComponent3D* other  = (caller == A) ? B : A;
                Contact pts[8];
                const int nc = caller->Shape->manifold(*other->Shape,
                                                       caller->position(), caller->scale(),
                                                       other->position(),  other->scale(), pts, 8);
                for (int c = 0; c < nc; ++c) buildConstraint(caller, other, pts[c], c, constraints);
            }
        }

        // 3b. Warm start — re-apply each contact's cached impulse so the solve starts near last
        //     frame's solution (lambdaN reaches the true support force immediately → friction budget).
        for (auto& c : constraints) {
            const glm::vec3 P = c.lambdaN * c.n + c.lambdaT1 * c.t1 + c.lambdaT2 * c.t2;
            c.A->applyImpulseAtArm(-P, c.rA);
            c.B->applyImpulseAtArm( P, c.rB);
        }

        // 4. Velocity LCP — PGS. Accumulate-clamp-apply-delta per contact; friction coupled to lambdaN.
        constexpr int kVelIterations = 12;
        for (int it = 0; it < kVelIterations; ++it)
            for (auto& c : constraints) { solveNormal(c); solveFriction(c); }

        // Store the converged impulses for next frame's warm start (fresh map drops stale contacts).
        auto& cache = impulseCache();
        cache.clear();
        for (auto& c : constraints) cache[c.key] = { c.lambdaN, c.lambdaT1, c.lambdaT2 };

        // 5. Integrate positions from the (now consistent) velocities.
        for (auto* b : world) {
            if (b->m_invMass == 0.0f) continue;
            auto* tf = b->owner->getComponent<TransformComponent3D>();
            tf->Position += b->Velocity * dt;
            if (b->Mode == Simulation::Complex) integrateOrientation(tf->Orientation, b->AngularVelocity, dt);
        }

        // 6. Split-impulse position correction — pushes out the detected overlap using PSEUDO
        //    velocities only, so the real velocity (and thus the resting state) is never disturbed.
        constexpr int kPosIterations = 4;
        for (int it = 0; it < kPosIterations; ++it)
            for (auto& c : constraints) solvePosition(c, dt);
        for (auto* b : world) {
            if (b->m_invMass == 0.0f) continue;
            if (b->m_biasVel == glm::vec3(0.0f) && b->m_biasAngVel == glm::vec3(0.0f)) continue;
            auto* tf = b->owner->getComponent<TransformComponent3D>();
            tf->Position += b->m_biasVel * dt;
            if (b->Mode == Simulation::Complex) integrateOrientation(tf->Orientation, b->m_biasAngVel, dt);
        }

        if (DebugPrint) {
            static int tick = 0;
            if (tick++ % 6 == 0)
                for (auto* b : world)
                    if (b->m_invMass != 0.0f) {
                        const glm::vec3 p = b->position();
                        std::cout << "[phys2] " << b->owner->Name
                                  << " pos(" << p.x << ", " << p.y << ", " << p.z << ")"
                                  << " |v|=" << glm::length(b->Velocity)
                                  << " |w|=" << glm::length(b->AngularVelocity) << "\n";
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
            glm::vec2 he{5.0f, 5.0f};
            if (s.contains("halfExtents") && s["halfExtents"].is_array() && s["halfExtents"].size() >= 2)
                he = { s["halfExtents"][0].get<float>(), s["halfExtents"][1].get<float>() };
            Shape = std::make_unique<PlaneShape>(n, infinite, he);
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
        Velocity += P * m_invMass;
        AngularVelocity += m_invInertiaWorld * glm::cross(r, P);
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
    };

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

    // Normal constraint: drive (vn + restitutionBias) -> 0, with the accumulated impulse clamped >= 0.
    static void solveNormal(ContactConstraint& c) {
        const float vn = glm::dot(pointVel(c.B, c.rB) - pointVel(c.A, c.rA), c.n);
        const float dl = -(vn + c.restitutionBias) * c.normalMass;
        const float old = c.lambdaN;
        c.lambdaN = std::max(0.0f, old + dl);
        const glm::vec3 P = (c.lambdaN - old) * c.n;
        c.A->applyImpulseAtArm(-P, c.rA);
        c.B->applyImpulseAtArm( P, c.rB);
    }

    // Boxed Coulomb friction on both tangents, clamped to +/- mu * accumulated normal impulse.
    static void solveFriction(ContactConstraint& c) {
        const float mu = std::sqrt(std::max(0.0f, c.A->Friction * c.B->Friction));
        const float limit = mu * c.lambdaN;
        // tangent 1
        {
            const float vt = glm::dot(pointVel(c.B, c.rB) - pointVel(c.A, c.rA), c.t1);
            const float old = c.lambdaT1;
            c.lambdaT1 = glm::clamp(old - vt * c.tanMass1, -limit, limit);
            const glm::vec3 P = (c.lambdaT1 - old) * c.t1;
            c.A->applyImpulseAtArm(-P, c.rA);
            c.B->applyImpulseAtArm( P, c.rB);
        }
        // tangent 2
        {
            const float vt = glm::dot(pointVel(c.B, c.rB) - pointVel(c.A, c.rA), c.t2);
            const float old = c.lambdaT2;
            c.lambdaT2 = glm::clamp(old - vt * c.tanMass2, -limit, limit);
            const glm::vec3 P = (c.lambdaT2 - old) * c.t2;
            c.A->applyImpulseAtArm(-P, c.rA);
            c.B->applyImpulseAtArm( P, c.rB);
        }
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
        // Apply to the bias (pseudo) velocities only.
        c.A->m_biasVel    -= P * c.A->m_invMass;
        c.A->m_biasAngVel -= c.A->m_invInertiaWorld * glm::cross(c.rA, P);
        c.B->m_biasVel    += P * c.B->m_invMass;
        c.B->m_biasAngVel += c.B->m_invInertiaWorld * glm::cross(c.rB, P);
    }

    // The new physics world: every v2 body (dynamic + static) lives here; the narrowphase sweeps it.
    static std::vector<RigidBodyComponent3D*>& bodies() {
        static std::vector<RigidBodyComponent3D*> v;
        return v;
    }
};

REGISTER_COMPONENT(RigidBodyComponent3D, "RigidBody3D")
