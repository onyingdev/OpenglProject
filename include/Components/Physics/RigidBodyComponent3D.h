#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include "Components/Rendering/MeshComponent.h"
#include "Components/Physics/ColliderComponent3D.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

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
// JSON: { "type": "RigidBody3D", "mass": 1.0, "friction": 0.5, "drag": 0.05,
//         "restitution": 0.3, "static": false, "useGravity": true, "gravity": [0,-9.81,0],
//         "simulation": "simple"|"complex", "angularDrag": 0.05,
//         "centerOfMass": [0,0,0], "initialVelocity": [0,0,0], "initialAngularVelocity": [0,0,0] }
class RigidBodyComponent3D : public SpatialComponent3D {
    MeshComponent*       mesh     = nullptr;
    ColliderComponent3D* collider = nullptr; // the body's surface; required

public:
    enum class Simulation { Simple, Complex };

    float      Mass        = 1.0f;            // <= 0 or Static => infinite mass (immovable)
    float      Friction    = 0.5f;            // Coulomb coefficient (combined geometric mean)
    float      Drag        = 0.1f;            // linear drag
    float      AngularDrag = 0.05f;           // angular drag (Complex only)
    float      Restitution = 0.3f;            // bounciness [0,1]
    bool       Static      = false;           // immovable: no integration, infinite mass
    bool       UseGravity  = true;
    Simulation Mode        = Simulation::Simple;
    glm::vec3  Gravity{0.0f, -9.81f, 0.0f};
    glm::vec3  CenterOfMass{0.0f};            // local offset from position (force application point)
    glm::vec3  Velocity{0.0f};                // linear velocity
    glm::vec3  AngularVelocity{0.0f};         // world angular velocity (Complex only)

    // --- force / impulse API -----------------------------------------------------
    void  AddForce(const glm::vec3& f)   { m_forceAccumulator += f; }
    void  AddImpulse(const glm::vec3& j) { Velocity += j * m_invMass; }
    void  AddTorque(const glm::vec3& t)  { m_torqueAccumulator += t; }
    void  SetVelocity(const glm::vec3& v){ Velocity = v; }

    // Force at a world point: linear force + torque about the center of mass. The torque term
    // is a no-op for Simple bodies (inverse-inertia is zero).
    void  AddForceAtPosition(const glm::vec3& f, const glm::vec3& worldPos) {
        AddForce(f);
        m_torqueAccumulator += glm::cross(worldPos - worldCenter(), f);
    }
    // Instantaneous impulse at a world point: changes both linear and angular velocity.
    void  AddImpulseAtPoint(const glm::vec3& P, const glm::vec3& worldPos) {
        applyImpulseAtArm(P, worldPos - worldCenter());
    }

    float inverseMass() const { return m_invMass; }

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Mass         = j.value("mass",        Mass);
        Friction     = j.value("friction",    Friction);
        Drag         = j.value("drag",        Drag);
        AngularDrag  = j.value("angularDrag", AngularDrag);
        Restitution  = j.value("restitution", Restitution);
        Static       = j.value("static",      Static);
        UseGravity   = j.value("useGravity",  UseGravity);
        // "simulation" (canonical) or "mode" (alias): "simple" (default) | "complex".
        const std::string sim = j.value("simulation", j.value("mode", std::string{"simple"}));
        if (sim == "complex") Mode = Simulation::Complex;
        Gravity         = jsonVec3(j.value("gravity",                nlohmann::json{}), Gravity);
        CenterOfMass    = jsonVec3(j.value("centerOfMass",           nlohmann::json{}), CenterOfMass);
        Velocity        = jsonVec3(j.value("initialVelocity",        nlohmann::json{}), Velocity);
        AngularVelocity = jsonVec3(j.value("initialAngularVelocity", nlohmann::json{}), AngularVelocity);
    }

    void start() override {
        SpatialComponent3D::start();
        mesh = owner->getComponent<MeshComponent>();
        if (!mesh) ComponentWarning::missing("RigidBodyComponent3D", "MeshComponent");
        collider = owner->getComponent<ColliderComponent3D>();
        if (!collider) throw MissingComponentException("RigidBodyComponent3D", "ColliderComponent3D");

        // One authoritative transform: position + quaternion orientation on the GameObject.
        this->useOwnerPosition();      this->useOwnerOrientation();
        collider->useOwnerPosition();  collider->useOwnerOrientation();
        if (mesh) { mesh->useOwnerPosition(); mesh->useOwnerOrientation(); }

        m_invMass = (Static || Mass <= 0.0f) ? 0.0f : 1.0f / Mass;
        if (CenterOfMass == glm::vec3(0.0f)) CenterOfMass = defaultCenterOfMass();
        m_invInertiaLocal = computeInvInertiaLocal();   // zero for Simple/Static/plane

        AllBodies().push_back(this);
    }

    ~RigidBodyComponent3D() override {
        auto& list = AllBodies();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    // One fixed physics slice: integrate every body, then sweep dynamic bodies against the
    // collider registry and resolve contacts. Call from the engine's fixed-timestep accumulator.
    static void stepWorld(float dt) {
        auto& bodies = AllBodies();
        for (auto* b : bodies) b->integrate(dt);

        const auto& colliders = ColliderComponent3D::colliders();
        for (auto* c : colliders) c->syncShape();   // refresh OBB/ball orientations + plane normals
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
    glm::vec3 m_torqueAccumulator{0.0f};
    float     m_invMass = 1.0f;
    glm::mat3 m_invInertiaLocal{0.0f};   // 0 => no angular response (Simple / Static / plane)
    glm::mat3 m_invInertiaWorld{0.0f};   // refreshed each step from orientation

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
    }

    // Apply an impulse P at world arm r (relative to the center of mass): linear + angular.
    void applyImpulseAtArm(const glm::vec3& P, const glm::vec3& r) {
        Velocity += P * m_invMass;
        AngularVelocity += m_invInertiaWorld * glm::cross(r, P);   // invInertiaWorld is 0 for Simple
    }

    // Velocity of a body's material point at world arm r (linear + rotational).
    static glm::vec3 pointVelocity(RigidBodyComponent3D* body, const glm::vec3& r) {
        return body ? body->Velocity + glm::cross(body->AngularVelocity, r) : glm::vec3(0.0f);
    }

    // Resolve one contact. `c.normal` points A -> B; `c.point` is the world contact point.
    // Uses contact-point impulses (linear + angular); for Simple bodies the angular terms
    // vanish (inverse-inertia 0) and this reduces exactly to the old linear resolution.
    static void resolve(RigidBodyComponent3D* A, RigidBodyComponent3D* B, const Contact& c) {
        const float invA   = A->m_invMass;
        const float invB   = B ? B->m_invMass : 0.0f;
        const float invSum = invA + invB;
        if (invSum <= 0.0f) return;

        const glm::vec3 n = c.normal;

        // 1. Positional correction (Baumgarte, linear) — push the pair apart along the normal.
        const float slop = 0.001f, percent = 0.8f;
        const float corr = std::max(c.penetration - slop, 0.0f) / invSum * percent;
        const glm::vec3 push = corr * n;
        A->owner->getComponent<TransformComponent3D>()->Position -= push * invA;
        if (B) B->owner->getComponent<TransformComponent3D>()->Position += push * invB;

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
        const float e  = std::min(A->Restitution, B ? B->Restitution : A->Restitution);
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
    }

    static std::vector<RigidBodyComponent3D*>& AllBodies() {
        static std::vector<RigidBodyComponent3D*> v;
        return v;
    }
};

REGISTER_COMPONENT(RigidBodyComponent3D, "RigidBody3D")
