#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include "Core/Engine/Scene.h"                 // TTL despawn: Scene::removeObject
#include "Components/Rendering/MeshComponent.h"
#include "Components/Physics/ColliderComponent3D.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

// DEPRECATED CLASS USE RigidBodyComponent3D

// RigidBody 3D. A spatial component backed by a Collider (its surface), a Mesh, a mass and
// (optionally) angular dynamics. Two per-body simulation modes:
//   - Simple  (default): LINEAR only — gravity, drag, forces/impulses, contact resolution at
//                        the body center. Cheap; the right choice for most gameplay objects.
//   - Complex: adds ANGULAR dynamics — an inverse-inertia tensor, torque, angular velocity +
//                        quaternion integration, and contact impulses applied at the contact
//                        point (so collisions/friction induce spin & rolling).
// The two share ONE solver: simple bodies keep inverse-inertia zero, which collapses the
// angular impulse math back to the linear case, so simple-mode behavior is identical to before.
//
// TRANSFORM AUTHORITY: body/collider/mesh all follow the owner TransformComponent3D via
// useOwnerPosition()/useOwnerOrientation(); the integrator writes Position and Orientation
// (quaternion) there, so collider detection and the rendered mesh track the physics pose.
//
// JSON: { "type": "OldRigidBody3D", "mass": 1.0, "friction": 0.5, "drag": 0.05,
//         "restitution": 0.3, "static": false, "useGravity": true, "gravity": [0,-9.81,0],
//         "simulation": "simple"|"complex", "angularDrag": 0.05,
//         "centerOfMass": [0,0,0], "initialVelocity": [0,0,0], "initialAngularVelocity": [0,0,0],
//         "ttl": 8.0, "ttlStart": "spawn"|"contact",          // despawn after ttl s (<=0 = permanent)
//         "freezeAfter": 3.0, "freezeStart": "spawn"|"contact" } // freeze static after N s (<=0 = never)
class OldRigidBodyComponent3D : public SpatialComponent3D {
    MeshComponent*       mesh     = nullptr;
    ColliderComponent3D* collider = nullptr; // the body's surface; required

public:
    enum class Simulation { Simple, Complex };
    enum class TimerStart { Spawn, Contact };   // when a lifecycle timer begins counting

    // Per-frame state dump of awake bodies in stepWorld(). When false the print block is skipped
    // entirely — zero overhead, no output. Flip to false to silence; set in code or via a toggle.
    inline static bool DebugPrint = false;

    float      Mass            = 1.0f;        // <= 0 or Static => infinite mass (immovable)
    float      Friction        = 0.5f;        // Coulomb (sliding) coefficient — combined geo mean
    float      RollingFriction = 0.05f;       // rolling resistance — damps spin so rollers stop (Complex)
    float      Drag            = 0.1f;        // linear drag
    float      AngularDrag     = 0.05f;       // angular drag (Complex only)
    float      Restitution = 0.3f;            // bounciness [0,1]
    bool       Static      = false;           // immovable: no integration, infinite mass
    bool       UseGravity  = true;
    bool       AllowSleep  = true;            // may this body go to sleep when it comes to rest?
    Simulation Mode        = Simulation::Simple;
    glm::vec3  Gravity{0.0f, -9.81f, 0.0f};
    glm::vec3  CenterOfMass{0.0f};            // local offset from position (force application point)
    glm::vec3  Velocity{0.0f};                // linear velocity
    glm::vec3  AngularVelocity{0.0f};         // world angular velocity (Complex only)

    // --- lifecycle timers (both optional, independent) ---------------------------
    float      TTL         = -1.0f;           // seconds to live, then despawn; <=0 = permanent
    float      FreezeAfter = -1.0f;           // seconds of free sim, then freeze static; <=0 = never
    TimerStart TtlStart    = TimerStart::Spawn;    // start the TTL clock on spawn or first contact
    TimerStart FreezeStart = TimerStart::Spawn;    // start the freeze clock on spawn or first contact

    // --- force / impulse API -----------------------------------------------------
    // Any external energy wakes the body so it resumes simulating.
    void  AddForce(const glm::vec3& f)   { wake(); m_forceAccumulator += f; }
    void  AddImpulse(const glm::vec3& j) { wake(); Velocity += j * m_invMass; }
    void  AddTorque(const glm::vec3& t)  { wake(); m_torqueAccumulator += t; }
    void  SetVelocity(const glm::vec3& v){ wake(); Velocity = v; }

    // Force at a world point: linear force + torque about the center of mass. The torque term
    // is a no-op for Simple bodies (inverse-inertia is zero).
    void  AddForceAtPosition(const glm::vec3& f, const glm::vec3& worldPos) {
        AddForce(f);   // wakes
        m_torqueAccumulator += glm::cross(worldPos - worldCenter(), f);
    }
    // Instantaneous impulse at a world point: changes both linear and angular velocity.
    void  AddImpulseAtPoint(const glm::vec3& P, const glm::vec3& worldPos) {
        wake();
        applyImpulseAtArm(P, worldPos - worldCenter());
    }

    // Sleep control. A sleeping body skips integration and resolution until something disturbs it.
    // A FROZEN body (freeze-window expired) ignores wake() entirely — it's permanently static.
    void  wake()            { if (m_frozen) return; m_asleep = false; m_sleepTimer = 0.0f; }
    bool  isAsleep() const  { return m_asleep; }
    bool  isFrozen() const  { return m_frozen; }

    float inverseMass() const { return m_invMass; }

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Mass            = j.value("mass",            Mass);
        Friction        = j.value("friction",        Friction);
        RollingFriction = j.value("rollingFriction", RollingFriction);
        Drag            = j.value("drag",            Drag);
        AngularDrag     = j.value("angularDrag",     AngularDrag);
        Restitution  = j.value("restitution", Restitution);
        Static       = j.value("static",      Static);
        UseGravity   = j.value("useGravity",  UseGravity);
        AllowSleep   = j.value("allowSleep",  AllowSleep);
        // "simulation" (canonical) or "mode" (alias): "simple" (default) | "complex".
        const std::string sim = j.value("simulation", j.value("mode", std::string{"simple"}));
        if (sim == "complex") Mode = Simulation::Complex;
        Gravity         = jsonVec3(j.value("gravity",                nlohmann::json{}), Gravity);
        CenterOfMass    = jsonVec3(j.value("centerOfMass",           nlohmann::json{}), CenterOfMass);
        Velocity        = jsonVec3(j.value("initialVelocity",        nlohmann::json{}), Velocity);
        AngularVelocity = jsonVec3(j.value("initialAngularVelocity", nlohmann::json{}), AngularVelocity);

        TTL         = j.value("ttl",         TTL);
        FreezeAfter = j.value("freezeAfter", FreezeAfter);
        TtlStart    = parseStart(j.value("ttlStart",    std::string{"spawn"}));
        FreezeStart = parseStart(j.value("freezeStart", std::string{"spawn"}));
    }

    // "contact" → start the clock on first contact; anything else (incl. "spawn") → on spawn.
    static TimerStart parseStart(const std::string& s) {
        return s == "contact" ? TimerStart::Contact : TimerStart::Spawn;
    }

    void start() override {
        SpatialComponent3D::start();
        std::cout << "[WARNING]: Deprecated component, use RigidBodyComponent3D" << std::endl;
        mesh = owner->getComponent<MeshComponent>();
        if (!mesh) ComponentWarning::missing("OldRigidBodyComponent3D", "MeshComponent");
        collider = owner->getComponent<ColliderComponent3D>();
        if (!collider) throw MissingComponentException("OldRigidBodyComponent3D", "ColliderComponent3D");

        // One authoritative transform: position + quaternion orientation on the GameObject.
        this->useOwnerPosition();      this->useOwnerOrientation();
        collider->useOwnerPosition();  collider->useOwnerOrientation();
        if (mesh) { mesh->useOwnerPosition(); mesh->useOwnerOrientation(); }

        m_invMass = (Static || Mass <= 0.0f) ? 0.0f : 1.0f / Mass;
        if (CenterOfMass == glm::vec3(0.0f)) CenterOfMass = defaultCenterOfMass();
        m_invInertiaLocal = computeInvInertiaLocal();   // zero for Simple/Static/plane

        (Static ? staticBodies() : dynamicBodies()).push_back(this);
    }

    ~OldRigidBodyComponent3D() override {
        auto& list = (Static ? staticBodies() : dynamicBodies());
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    // One fixed physics slice. The DYNAMIC world (dynamicBodies()) integrates and drives
    // resolution; the STATIC world (static bodies + bare colliders, reached via the collider
    // registry) is immovable and never integrates. Static x static is never tested because the
    // outer loop is dynamic-only. Call from the engine's fixed-timestep accumulator.
    static void stepWorld(float dt) {
        auto& dyn = dynamicBodies();

        // 1. Integrate the dynamic world. Each body touches only its own state, so this
        //    parallelizes cleanly. Guarded (like HurtBox) so small scenes stay serial and
        //    don't pay thread spin-up; degrades to a plain loop when built without OpenMP.
        #pragma omp parallel for schedule(static) if(dyn.size() > 256)
        for (size_t i = 0; i < dyn.size(); ++i) dyn[i]->integrate(dt);

        const auto& colliders = ColliderComponent3D::colliders();
        // Refresh OBB/ball orientations + plane normals — per-collider, independent.
        #pragma omp parallel for schedule(static) if(colliders.size() > 256)
        for (size_t i = 0; i < colliders.size(); ++i) colliders[i]->syncShape();

        // 2. DETECTION — parallel, mirroring HurtBox::tickWorld. Each dynamic body collects its
        //    contact manifolds into its OWN row (m_contacts), so threads never write shared state:
        //    manifold() is const and reads only the (already-synced) shapes + start-of-step poses.
        //    1 point for most pairs; up to 8 corners for box/OBB-vs-plane so a flat box rests on a
        //    balanced set of points instead of rocking on one.
        #pragma omp parallel for schedule(guided) if(dyn.size() > 256)
        for (size_t i = 0; i < dyn.size(); ++i) {
            auto* a = dyn[i];
            a->m_contacts.clear();
            if (a->m_asleep || a->m_invMass == 0.0f || !a->collider || !a->collider->Shape) continue;
            for (auto* other : colliders) {
                if (other == a->collider || !other->Shape) continue;
                if (!ColliderComponent3D::masksOverlap(a->collider, other)) continue;

                OldRigidBodyComponent3D* otherBody = other->owner->getComponent<OldRigidBodyComponent3D>();
                const bool otherDynamic = otherBody && otherBody->m_invMass != 0.0f;
                // Resolve each AWAKE dynamic pair once (lower pointer drives). Don't defer to a
                // SLEEPING otherBody — it's skipped as a driver, so the awake `a` must handle the
                // pair itself (and resolve() wakes it), else the contact is dropped and a phases through.
                if (otherDynamic && !otherBody->m_asleep && a >= otherBody) continue;

                ContactBatch batch;
                batch.other = otherBody;
                batch.nc = a->collider->Shape->manifold(
                    *other->Shape,
                    a->collider->position(), a->collider->scale(),
                    other->position(),       other->scale(),
                    batch.contacts, 8);
                if (batch.nc > 0) a->m_contacts.push_back(batch);
            }
        }

        // 3. RESOLUTION — serial, and it MUST be: resolve() mutates BOTH bodies' velocity and
        //    position, so two threads sharing an `other` would race. Same body/contact order as
        //    the old interleaved loop, so resting behaviour is preserved (a body touching a single
        //    collider — e.g. the box on the ground — is bit-for-bit identical; only a body hitting
        //    several colliders at once now solves against start-of-step poses, the standard
        //    detect-then-solve order). Sequential-impulse iterations converge the per-corner
        //    impulses; the positional push-out is split across the manifold and applied once.
        //    A body can have SEVERAL manifolds at once (e.g. a box resting on the ground WITH a box
        //    on top of it), and in a stack those manifolds span multiple driver bodies. They must
        //    be solved TOGETHER, so the iteration loop is the OUTERMOST loop and cycles over EVERY
        //    contact each pass (global sequential impulse). Solving one manifold to convergence
        //    before the next lets them fight — the box-on-top impulse undoes the ground solution
        //    every step, so the stack never settles and the top box spins up. Position correction
        //    (Baumgarte) is applied once per contact, on the first iteration only.
        constexpr int kSolverIterations = 8;
        for (int it = 0; it < kSolverIterations; ++it)
            for (auto* a : dyn)
                for (const auto& batch : a->m_contacts) {
                    const float posScale = (batch.nc > 1) ? 1.0f / static_cast<float>(batch.nc) : 1.0f;
                    for (int i = 0; i < batch.nc; ++i)
                        resolve(a, batch.other, batch.contacts[i], posScale, it == 0);
                }

        // Debug: dump the state of every AWAKE (non-resting) body. Gated by DebugPrint — when it's
        // false this whole block is skipped, so it leaves no footprint (no I/O, no counter tick).
        // Runs after resolution (velocities are post-solve) and before the sleep pass clears the
        // contact flags, so `pairs`/`nc` reflect this step. Throttled so it stays readable.
        if (DebugPrint) {
            static int s_dbgTick = 0;
            constexpr int kEvery = 6;                  // ~10 dumps/sec at a 60 Hz step
            if (s_dbgTick++ % kEvery == 0) {
                for (auto* b : dyn) {
                    if (b->m_asleep) continue;         // only non-resting bodies
                    int nc = 0; for (const auto& bt : b->m_contacts) nc += bt.nc;
                    const glm::vec3 p = b->position();
                    std::cout << "[phys] " << b->owner->Name
                              << " pos(" << p.x << ", " << p.y << ", " << p.z << ")"
                              << " |v|="  << glm::length(b->Velocity)
                              << " |w|="  << glm::length(b->AngularVelocity)
                              << " pairs=" << b->m_contacts.size()
                              << " nc="    << nc << "\n";
                }
            }
        }

        // 4. Sleep pass — runs AFTER resolution, where a supported body's velocity is ~0 (the normal
        //    impulse cancelled the gravity velocity). A body that's been supported (m_hadContact) and
        //    below the speed thresholds for kSleepDelay is put to sleep: zeroed and skipped next step
        //    until a force/impulse or an incoming contact wakes it. Per-body → parallel-safe.
        constexpr float kLinSleepSq = 0.05f * 0.05f, kAngSleepSq = 0.06f * 0.06f, kSleepDelay = 0.4f;
        #pragma omp parallel for schedule(static) if(dyn.size() > 256)
        for (size_t i = 0; i < dyn.size(); ++i) {
            auto* b = dyn[i];
            if (b->m_asleep) continue;
            const float linSq = glm::dot(b->Velocity, b->Velocity);
            const float angSq = glm::dot(b->AngularVelocity, b->AngularVelocity);
            if (b->m_hadContact && linSq < kLinSleepSq && angSq < kAngSleepSq) b->m_sleepTimer += dt;
            else                                                               b->m_sleepTimer = 0.0f;
            if (b->AllowSleep && b->m_sleepTimer >= kSleepDelay) {
                b->m_asleep = true;
                b->Velocity = glm::vec3(0.0f);
                b->AngularVelocity = glm::vec3(0.0f);
            }
            if (b->m_hadContact) b->m_everContacted = true;   // latches the "contact" timer trigger
            b->m_hadContact = false;   // reset; resolve() re-sets it this/next step
        }

        // 5. Lifecycle timers — SERIAL (appends to the shared despawn list). Each timer advances only
        //    once armed (spawn = immediately, contact = after the body's first contact). TTL marks the
        //    body for despawn; FreezeAfter freezes it in place (safe: freeze() touches only its own
        //    state). Frozen bodies stay in dyn (cheaply skipped) so their TTL can still elapse.
        for (auto* b : dyn) {
            if (b->TTL > 0.0f) {
                const bool armed = (b->TtlStart == TimerStart::Spawn) || b->m_everContacted;
                if (armed && (b->m_ttlElapsed += dt) >= b->TTL) pendingDespawn().push_back(b);
            }
            if (!b->m_frozen && b->FreezeAfter > 0.0f) {
                const bool armed = (b->FreezeStart == TimerStart::Spawn) || b->m_everContacted;
                if (armed && (b->m_freezeElapsed += dt) >= b->FreezeAfter) b->freeze();
            }
        }

        // Deferred despawn drain — at the very end of the step (no active body-loop, and scenes have
        // already updated this frame) so removing the GameObject is safe. removeObject frees it;
        // ~OldRigidBodyComponent3D / ~ColliderComponent3D unregister from their registries.
        auto& dead = pendingDespawn();
        for (auto* b : dead) {
            GameObject* o = b->owner;
            Scene* sc = o->scene();
            if (!sc) { std::cerr << "[RigidBody] TTL despawn: \"" << o->Name << "\" has no scene — left in place\n"; continue; }
            const std::string name = o->Name;   // copy: removeObject frees o, and o->Name lives inside it
            sc->removeObject(name);
        }
        dead.clear();
    }

private:
    glm::vec3 m_forceAccumulator{0.0f};
    glm::vec3 m_torqueAccumulator{0.0f};
    float     m_invMass = 1.0f;
    glm::mat3 m_invInertiaLocal{0.0f};   // 0 => no angular response (Simple / Static / plane)
    glm::mat3 m_invInertiaWorld{0.0f};   // refreshed each step from orientation
    bool      m_hadContact = false;      // got a contact this step (supported => may sleep)
    bool      m_asleep     = false;      // skips integration + resolution until woken
    float     m_sleepTimer = 0.0f;       // seconds spent supported + below the sleep thresholds

    // Lifecycle-timer state.
    bool      m_everContacted = false;   // has touched something at least once (arms "contact" timers)
    bool      m_frozen        = false;   // freeze window elapsed => permanently static
    float     m_ttlElapsed    = 0.0f;    // seconds the TTL clock has run (once armed)
    float     m_freezeElapsed = 0.0f;    // seconds the freeze clock has run (once armed)

    // Per-body contact scratch: filled in the PARALLEL detection phase (each body writes only
    // its own row, so no locking), drained in the SERIAL resolution phase. cleared() — not freed —
    // each step so the capacity is reused. Contact[8] matches the manifold cap (box/OBB corners).
    struct ContactBatch {
        OldRigidBodyComponent3D* other;     // the other body; null for a bare/static collider
        int     nc;                      // contact count in this manifold
        Contact contacts[8];
    };
    std::vector<ContactBatch> m_contacts;

    glm::vec3 defaultCenterOfMass() const { return glm::vec3(0.0f); }

    // World center of mass (position + oriented local offset). CoM defaults to the origin.
    glm::vec3 worldCenter() const {
        return position() + glm::mat3_cast(orientation()) * CenterOfMass;
    }

    // Inverse inertia tensor in LOCAL (body) space, from the collider shape × mass. Zero for
    // Simple/Static bodies and shapes without a sensible solid (plane), which keeps them linear.
    glm::mat3 computeInvInertiaLocal() const {
        if (Mode != Simulation::Complex || m_invMass == 0.0f || !collider || !collider->Shape)
            return glm::mat3(0.0f);
        ColliderShape* s = collider->Shape.get();
        const glm::vec3 sc = collider->scale();
        const float m = Mass;
        switch (s->kind()) {
            case ShapeKind::Box:
            case ShapeKind::OBB: {
                const glm::vec3 he = (s->kind() == ShapeKind::Box)
                    ? static_cast<BoxShape*>(s)->HalfExtents
                    : static_cast<OBBShape*>(s)->HalfExtents;
                const glm::vec3 w = 2.0f * he * sc;   // full extents
                const float ix = (1.0f/12.0f) * m * (w.y*w.y + w.z*w.z);
                const float iy = (1.0f/12.0f) * m * (w.x*w.x + w.z*w.z);
                const float iz = (1.0f/12.0f) * m * (w.x*w.x + w.y*w.y);
                return glm::mat3(glm::vec3(1.0f/ix, 0, 0), glm::vec3(0, 1.0f/iy, 0), glm::vec3(0, 0, 1.0f/iz));
            }
            case ShapeKind::Sphere:
            case ShapeKind::Ball: {
                const float r = static_cast<SphereShape*>(s)->Radius * sc.x;
                const float i = (2.0f/5.0f) * m * r * r;
                return glm::mat3(i > 0.0f ? 1.0f/i : 0.0f);   // (2/5)mr^2 * I, inverted
            }
            default: return glm::mat3(0.0f);   // plane: treat as no rotational response
        }
    }

    // Semi-implicit Euler. Writes Position always; for Complex bodies also integrates the
    // orientation quaternion from angular velocity. Static bodies don't move.
    void integrate(float dt) {
        if (m_asleep) return;                                  // frozen: gravity can't perturb it
        if (m_invMass == 0.0f) { m_forceAccumulator = glm::vec3(0.0f); m_torqueAccumulator = glm::vec3(0.0f); return; }

        // Linear.
        const glm::vec3 accel = m_forceAccumulator * m_invMass + (UseGravity ? Gravity : glm::vec3(0.0f));
        Velocity += accel * dt;
        Velocity *= glm::clamp(1.0f - Drag * dt, 0.0f, 1.0f);
        owner->getComponent<TransformComponent3D>()->Position += Velocity * dt;
        m_forceAccumulator = glm::vec3(0.0f);

        // Angular (Complex only; m_invInertiaLocal is zero otherwise).
        if (Mode == Simulation::Complex) {
            glm::quat& q = owner->getComponent<TransformComponent3D>()->Orientation;
            const glm::mat3 R = glm::mat3_cast(q);
            m_invInertiaWorld = R * m_invInertiaLocal * glm::transpose(R);
            AngularVelocity += (m_invInertiaWorld * m_torqueAccumulator) * dt;
            AngularVelocity *= glm::clamp(1.0f - AngularDrag * dt, 0.0f, 1.0f);
            const glm::quat spin(0.0f, AngularVelocity.x, AngularVelocity.y, AngularVelocity.z);
            q = glm::normalize(q + 0.5f * spin * q * dt);
            m_torqueAccumulator = glm::vec3(0.0f);
        }
        // Resting/sleep is decided in stepWorld's post-resolution pass (where a supported body's
        // velocity is ~0), not here — gravity has just been added, so it's never ~0 at this point.
    }

    // Freeze the body into permanent static geometry: stop it dead and give it infinite mass so the
    // integrator and detection skip it (m_invMass == 0 / m_asleep) while the collider stays
    // registered — other bodies still land and rest on it. wake() is guarded so it never revives.
    // Only mutates this body's own state, so it's safe to call mid-pass.
    void freeze() {
        Velocity           = glm::vec3(0.0f);
        AngularVelocity    = glm::vec3(0.0f);
        m_invMass          = 0.0f;
        m_invInertiaLocal  = glm::mat3(0.0f);
        m_invInertiaWorld  = glm::mat3(0.0f);
        m_frozen           = true;
        m_asleep           = true;
    }

    // Apply an impulse P at world arm r (relative to the center of mass): linear + angular.
    void applyImpulseAtArm(const glm::vec3& P, const glm::vec3& r) {
        Velocity += P * m_invMass;
        AngularVelocity += m_invInertiaWorld * glm::cross(r, P);   // invInertiaWorld is 0 for Simple
    }

    // Rolling resistance: a bounded counter-impulse opposing spin, so a rolling body slows and
    // stops (sliding friction alone can't — a pure roll has zero contact-point slip). `budget`
    // (= muRoll * normalImpulse) is scaled by the lever arm to torque units and never overshoots
    // past zero. No-op for Simple/static bodies (m_invInertiaWorld is zero).
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

    // Velocity of a body's material point at world arm r (linear + rotational).
    static glm::vec3 pointVelocity(OldRigidBodyComponent3D* body, const glm::vec3& r) {
        return body ? body->Velocity + glm::cross(body->AngularVelocity, r) : glm::vec3(0.0f);
    }

    // Resolve one contact. `c.normal` points A -> B; `c.point` is the world contact point.
    // Uses contact-point impulses (linear + angular); for Simple bodies the angular terms
    // vanish (inverse-inertia 0) and this reduces exactly to the old linear resolution.
    static void resolve(OldRigidBodyComponent3D* A, OldRigidBodyComponent3D* B, const Contact& c,
                        float posScale = 1.0f, bool applyPosition = true) {
        const float invA   = A->m_invMass;
        const float invB   = B ? B->m_invMass : 0.0f;
        const float invSum = invA + invB;
        if (invSum <= 0.0f) return;

        const glm::vec3 n = c.normal;

        // 1. Positional correction (Baumgarte, linear) — push the pair apart along the normal.
        //    Applied ONCE per step (first velocity iteration only); posScale splits it across a
        //    multi-point manifold so it doesn't over-push.
        if (applyPosition) {
            const float slop = 0.001f, percent = 0.8f;
            const float corr = std::max(c.penetration - slop, 0.0f) / invSum * percent * posScale;
            const glm::vec3 push = corr * n;
            A->owner->getComponent<TransformComponent3D>()->Position -= push * invA;
            if (B) B->owner->getComponent<TransformComponent3D>()->Position += push * invB;
        }

        // Contact arms (relative to each center of mass).
        const glm::vec3 rA = c.point - A->worldCenter();
        const glm::vec3 rB = c.point - (B ? B->worldCenter() : c.point);
        const glm::mat3 IiA = A->m_invInertiaWorld;
        const glm::mat3 IiB = B ? B->m_invInertiaWorld : glm::mat3(0.0f);

        // 2. Normal impulse with restitution (effective mass includes the angular term).
        glm::vec3 vrel = pointVelocity(B, rB) - pointVelocity(A, rA);
        const float velN = glm::dot(vrel, n);
        if (velN > 0.0f) return;                        // separating
        const glm::vec3 rnA = glm::cross(rA, n), rnB = glm::cross(rB, n);
        const float kN = invSum + glm::dot(n, glm::cross(IiA * rnA, rA))
                                + glm::dot(n, glm::cross(IiB * rnB, rB));
        // Restitution only for real impacts: below a small approach speed (resting / settling
        // under gravity) treat e=0, else the bounce re-injects velocity every step and the body
        // jitters forever instead of coming to rest.
        constexpr float kRestitutionThreshold = 0.5f;   // m/s
        float e = std::min(A->Restitution, B ? B->Restitution : A->Restitution);
        if (-velN < kRestitutionThreshold) e = 0.0f;
        const float jN = -(1.0f + e) * velN / kN;
        const glm::vec3 Pn = jN * n;
        A->applyImpulseAtArm(-Pn, rA);
        if (B) B->applyImpulseAtArm(Pn, rB);

        // 3. Friction: tangential impulse clamped to the Coulomb cone.
        vrel = pointVelocity(B, rB) - pointVelocity(A, rA);
        glm::vec3 t = vrel - glm::dot(vrel, n) * n;
        const float tl = glm::length(t);
        if (tl > 1e-6f) {
            t /= tl;
            const glm::vec3 rtA = glm::cross(rA, t), rtB = glm::cross(rB, t);
            const float kT = invSum + glm::dot(t, glm::cross(IiA * rtA, rA))
                                    + glm::dot(t, glm::cross(IiB * rtB, rB));
            float jt = -glm::dot(vrel, t) / kT;
            const float mu = std::sqrt(A->Friction * (B ? B->Friction : A->Friction));
            jt = glm::clamp(jt, -jN * mu, jN * mu);
            const glm::vec3 Pt = jt * t;
            A->applyImpulseAtArm(-Pt, rA);
            if (B) B->applyImpulseAtArm(Pt, rB);
        }

        // 4. Rolling resistance — damp spin so a rolling body comes to rest.
        const float muRoll = std::sqrt(A->RollingFriction * (B ? B->RollingFriction : A->RollingFriction));
        if (muRoll > 0.0f) {
            A->applyRollingResistance(rA, muRoll * jN);
            if (B) B->applyRollingResistance(rB, muRoll * jN);
        }

        // Mark both as supported this step (gates sleeping). An awake A contacting a sleeping B
        // wakes B so it resumes simulating (A is never asleep here — skipped in the outer loop).
        A->m_hadContact = true;
        if (B) { B->m_hadContact = true; if (B->m_asleep) B->wake(); }
    }

    // The two physics worlds. Dynamic bodies integrate + drive resolution; static bodies are
    // immovable world geometry (collided against via the collider registry, never integrated).
    static std::vector<OldRigidBodyComponent3D*>& dynamicBodies() {
        static std::vector<OldRigidBodyComponent3D*> v;
        return v;
    }
    static std::vector<OldRigidBodyComponent3D*>& staticBodies() {
        static std::vector<OldRigidBodyComponent3D*> v;
        return v;
    }

    // Bodies whose TTL elapsed this step; drained (their GameObjects removed) at the end of
    // stepWorld, where it's safe — no body-loop is active and scenes have already updated.
    static std::vector<OldRigidBodyComponent3D*>& pendingDespawn() {
        static std::vector<OldRigidBodyComponent3D*> v;
        return v;
    }
};

REGISTER_COMPONENT(OldRigidBodyComponent3D, "OldRigidBody3D")
