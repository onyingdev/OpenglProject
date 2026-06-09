#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include "Components/Rendering/MeshComponent.h"
#include "Components/Physics/ColliderComponent3D.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

// RigidBody 3D — linear dynamics. A rigid body is a spatial component backed by a
// Collider (its "surface"), a Mesh (what you see), a mass and a center of mass. It
// moves only through forces/impulses + gravity + drag; rotation from physics (torque,
// angular velocity, inertia) is a deferred phase, so CenterOfMass is stored as the
// force-application point but does not yet produce spin.
//
// TRANSFORM AUTHORITY: SpatialComponent3D::start() gives every spatial component its
// own snapshot of the transform. To make physics actually move the visible mesh AND the
// collider used for detection, this body, its collider and its mesh are all pointed at
// the GameObject's single TransformComponent3D via useOwnerPosition(); integration writes
// straight to that Position, so collider->position() reflects the new pose immediately.
//
// JSON: { "type": "RigidBody3D", "mass": 1.0, "friction": 0.5, "drag": 0.05,
//         "restitution": 0.3, "static": false, "useGravity": true,
//         "gravity": [0,-9.81,0], "centerOfMass": [0,0,0], "initialVelocity": [0,0,0] }
class RigidBodyComponent3D : public SpatialComponent3D {
    MeshComponent*       mesh     = nullptr;
    ColliderComponent3D* collider = nullptr; // the body's surface; required

public:
    float     Mass        = 1.0f;            // <= 0 or Static => infinite mass (immovable)
    float     Friction    = 0.5f;            // Coulomb coefficient (combined geometric mean)
    float     Drag        = 0.1f;            // linear drag, simulating air resistance
    float     Restitution = 0.3f;            // bounciness [0,1]
    bool      Static      = false;           // immovable: no integration, infinite mass
    bool      UseGravity  = true;
    glm::vec3 Gravity{0.0f, -9.81f, 0.0f};   // per-body acceleration when UseGravity
    glm::vec3 CenterOfMass{0.0f};            // local offset from position (force point; angular phase)
    glm::vec3 Velocity{0.0f};                // current linear velocity

    // --- force API ---------------------------------------------------------------
    // AddForce accumulates over the step (cleared each integrate); AddImpulse is an
    // instantaneous change in velocity (consumed immediately, not cleared).
    void  AddForce(const glm::vec3& f)   { m_forceAccumulator += f; }
    void  AddImpulse(const glm::vec3& j) { Velocity += j * m_invMass; }
    void  SetVelocity(const glm::vec3& v){ Velocity = v; }

    // Linear-only: the application point is ignored for now (no torque). Documented hook
    // for the future angular phase: torque += cross(worldPos - worldCenterOfMass, f).
    void  AddForceAtPosition(const glm::vec3& f, const glm::vec3& /*worldPos*/) { AddForce(f); }

    float inverseMass() const { return m_invMass; }

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Mass         = j.value("mass",        Mass);
        Friction     = j.value("friction",    Friction);
        Drag         = j.value("drag",        Drag);
        Restitution  = j.value("restitution", Restitution);
        Static       = j.value("static",      Static);
        UseGravity   = j.value("useGravity",  UseGravity);
        Gravity      = jsonVec3(j.value("gravity",         nlohmann::json{}), Gravity);
        CenterOfMass = jsonVec3(j.value("centerOfMass",    nlohmann::json{}), CenterOfMass);
        Velocity     = jsonVec3(j.value("initialVelocity", nlohmann::json{}), Velocity);
    }

    void start() override {
        SpatialComponent3D::start();
        mesh = owner->getComponent<MeshComponent>();
        if (!mesh) ComponentWarning::missing("RigidBodyComponent3D", "MeshComponent");
        collider = owner->getComponent<ColliderComponent3D>();
        if (!collider) throw MissingComponentException("RigidBodyComponent3D", "ColliderComponent3D");

        // One authoritative transform: the GameObject's TransformComponent3D.Position.
        // Body, collider and mesh all read it; the solver writes it.
        this->useOwnerPosition();
        collider->useOwnerPosition();
        if (mesh) mesh->useOwnerPosition();

        m_invMass = (Static || Mass <= 0.0f) ? 0.0f : 1.0f / Mass;
        if (CenterOfMass == glm::vec3(0.0f)) CenterOfMass = defaultCenterOfMass();

        AllBodies().push_back(this);
    }

    ~RigidBodyComponent3D() override {
        auto& list = AllBodies();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    // One fixed physics slice: integrate every body, then sweep dynamic bodies against
    // the collider registry and resolve contacts. Call from the engine's fixed-timestep
    // accumulator (Phase 4), NOT per render frame. O(N*M) — broadphase is future work.
    static void stepWorld(float dt) {
        auto& bodies = AllBodies();

        // 1. Integrate. Statics no-op inside integrate() (invMass 0).
        for (auto* b : bodies) b->integrate(dt);

        // 2. Narrowphase + resolve. Only DYNAMIC bodies drive resolution; static bodies
        //    and bare colliders (no RigidBody) are encountered as the "other" side and
        //    act as infinite mass. Dynamic-dynamic pairs are resolved once (lower pointer).
        const auto& colliders = ColliderComponent3D::colliders();
        for (auto* a : bodies) {
            if (a->m_invMass == 0.0f || !a->collider || !a->collider->Shape) continue;
            for (auto* other : colliders) {
                if (other == a->collider || !other->Shape) continue;
                if (!ColliderComponent3D::masksOverlap(a->collider, other)) continue;

                RigidBodyComponent3D* otherBody = other->owner->getComponent<RigidBodyComponent3D>();
                const bool otherDynamic = otherBody && otherBody->m_invMass != 0.0f;
                if (otherDynamic && a >= otherBody) continue;   // resolve dynamic pair once

                const Contact c = a->collider->Shape->collide(
                    *other->Shape,
                    a->collider->position(), a->collider->scale(),
                    other->position(),       other->scale());
                if (c.hit) resolve(a, otherBody, c);
            }
        }
    }

private:
    glm::vec3 m_forceAccumulator{0.0f};
    float     m_invMass = 1.0f;

    // Both supported shapes are centered on the collider origin, so the local center of
    // mass is (0,0,0). Hook for asymmetric/compound shapes later.
    glm::vec3 defaultCenterOfMass() const { return glm::vec3(0.0f); }

    // Semi-implicit (symplectic) Euler over a fixed slice. Writes the authoritative
    // Position so collider/mesh follow. Static bodies don't move; their forces are dropped.
    void integrate(float dt) {
        if (m_invMass == 0.0f) { m_forceAccumulator = glm::vec3(0.0f); return; }
        const glm::vec3 accel = m_forceAccumulator * m_invMass
                              + (UseGravity ? Gravity : glm::vec3(0.0f));
        Velocity += accel * dt;
        Velocity *= glm::clamp(1.0f - Drag * dt, 0.0f, 1.0f);   // linear drag
        owner->getComponent<TransformComponent3D>()->Position += Velocity * dt;
        m_forceAccumulator = glm::vec3(0.0f);                   // forces cleared; impulses already applied
    }

    // Resolve a single contact between dynamic body A and B (another body, possibly
    // static; or nullptr for a bare collider). `c.normal` points A -> B. Three steps:
    // positional push-out (anti-sink), normal impulse (restitution), Coulomb friction.
    static void resolve(RigidBodyComponent3D* A, RigidBodyComponent3D* B, const Contact& c) {
        const float invA   = A->m_invMass;
        const float invB   = B ? B->m_invMass : 0.0f;
        const float invSum = invA + invB;
        if (invSum <= 0.0f) return;                 // both immovable

        const glm::vec3 n = c.normal;               // A -> B

        // 1. Positional correction (Baumgarte): leave a little slop to avoid jitter, then
        //    push the pair apart along the normal, split by inverse mass.
        const float slop = 0.001f, percent = 0.8f;
        const float corr = std::max(c.penetration - slop, 0.0f) / invSum * percent;
        const glm::vec3 push = corr * n;
        A->owner->getComponent<TransformComponent3D>()->Position -= push * invA;
        if (B) B->owner->getComponent<TransformComponent3D>()->Position += push * invB;

        // 2. Normal impulse with restitution.
        glm::vec3 rv = (B ? B->Velocity : glm::vec3(0.0f)) - A->Velocity;  // relative vel B - A
        const float velN = glm::dot(rv, n);
        if (velN > 0.0f) return;                    // already separating; no impulse/friction
        const float e    = std::min(A->Restitution, B ? B->Restitution : A->Restitution);
        const float jImp = -(1.0f + e) * velN / invSum;
        const glm::vec3 impulse = jImp * n;
        A->Velocity -= impulse * invA;
        if (B) B->Velocity += impulse * invB;

        // 3. Friction: tangential impulse, clamped to Coulomb cone (combined = geo mean).
        rv = (B ? B->Velocity : glm::vec3(0.0f)) - A->Velocity;
        glm::vec3 t = rv - glm::dot(rv, n) * n;
        const float tLen = glm::length(t);
        if (tLen > 1e-6f) {
            t /= tLen;
            float jt = -glm::dot(rv, t) / invSum;
            const float mu = std::sqrt(A->Friction * (B ? B->Friction : A->Friction));
            jt = glm::clamp(jt, -jImp * mu, jImp * mu);
            const glm::vec3 fImp = jt * t;
            A->Velocity -= fImp * invA;
            if (B) B->Velocity += fImp * invB;
        }
    }

    // Registry of live bodies — drives integration and avoids double-resolving pairs.
    // Mirrors ColliderComponent3D::AllColliders(). Used by stepWorld() (Phase 3).
    static std::vector<RigidBodyComponent3D*>& AllBodies() {
        static std::vector<RigidBodyComponent3D*> v;
        return v;
    }
};

REGISTER_COMPONENT(RigidBodyComponent3D, "RigidBody3D")
