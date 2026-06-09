#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include <glm/glm.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cmath>
#ifdef _OPENMP
#include <omp.h>
#endif

// This is kinda vibecoded needs revision, its implemented using the double dispatch pattern for two way collision check.
// TODO: check code and optimize multithread.
// BUG: well more than a BUG is an optimization. Optimize the code using OpenMP. Sometimes the collider stutter on enter or exit, but that its due to the std::cout.

// Phase 1 skeleton:
//   - One primitive shape (AABB box), with a hierarchy so Sphere/Capsule slot in later
//   - Static registry of all live colliders, swept O(N^2) once per frame via tickWorld()
//   - Layer/Mask filtering
//   - OnEnter / OnStay / OnExit callbacks (computed from frame-to-frame diff)
//   - Trigger-only: no push-out / no velocity changes (that lives in a future RigidBody)
//
// Not yet implemented (future phases): Sphere/Capsule/Cylinder/OBB, broadphase,
// debug rendering, contact manifolds, mesh shapes.

class ColliderComponent3D;

// -----------------------------------------------------------------------------
// Shape hierarchy
// -----------------------------------------------------------------------------
// Each concrete shape owns its parameters (half-extents, radius, ...). World
// transform is supplied at test time so the shape itself stays pose-free.

class BoxShape;
class SphereShape;
class PlaneShape;

// Result of a narrowphase test. Unlike intersects() (bool), this carries the data a
// rigid-body solver needs to respond: the separating axis and how deep the overlap is.
// CONVENTION: for `a.collide(b, ...)`, `normal` is a unit vector pointing FROM a TOWARD b
// (so a separates by moving along -normal, b along +normal). `penetration` is the overlap
// depth (the minimum translation distance). `point` is an approximate world contact point,
// unused by the linear solver but kept for the future angular phase.
struct Contact {
    bool      hit         = false;
    glm::vec3 normal{0.0f};
    float     penetration = 0.0f;
    glm::vec3 point{0.0f};
};

class ColliderShape {
public:
    virtual ~ColliderShape() = default;

    // Double dispatch entry point. Implementations forward to the right pair.
    virtual bool intersects(const ColliderShape& other,
                            const glm::vec3& selfPos,  const glm::vec3& selfScale,
                            const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;

    // Per-type back-dispatch hooks. Add one per new shape (sphere, capsule, ...).
    virtual bool intersectsBox(const BoxShape& other,
                               const glm::vec3& selfPos,  const glm::vec3& selfScale,
                               const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;

    virtual bool intersectsSphere(const SphereShape& other,
                                  const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                  const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;

    virtual bool intersectsPlane(const PlaneShape& other,
                                 const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                 const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;

    // Contact-producing variant of the dispatch above. Runs PARALLEL to intersects()
    // (the trigger path still uses the bool version) so the physics solver can ask for a
    // normal + penetration. Same forwarding shape; each level that re-dispatches to
    // `other.collideX(...)` negates the returned normal to undo the A/B swap (a Contact
    // is directional, unlike a symmetric bool). See implementations below.
    virtual Contact collide(const ColliderShape& other,
                            const glm::vec3& selfPos,  const glm::vec3& selfScale,
                            const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;

    virtual Contact collideBox(const BoxShape& other,
                               const glm::vec3& selfPos,  const glm::vec3& selfScale,
                               const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;

    virtual Contact collideSphere(const SphereShape& other,
                                  const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                  const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;

    virtual Contact collidePlane(const PlaneShape& other,
                                 const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                 const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;
};

class BoxShape : public ColliderShape {
public:
    glm::vec3 HalfExtents{0.5f}; // local half-size; world size = HalfExtents * scale

    explicit BoxShape(glm::vec3 halfExtents = glm::vec3(0.5f)) : HalfExtents(halfExtents) {}

    bool intersects(const ColliderShape& other,
                    const glm::vec3& selfPos,  const glm::vec3& selfScale,
                    const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        // Forward to the other shape's box-handler. When SphereShape exists, this
        // pattern lets it implement intersectsBox(box, ...) without touching BoxShape.
        return other.intersectsBox(*this, otherPos, otherScale, selfPos, selfScale);
    }

    bool intersectsBox(const BoxShape& other,
                        const glm::vec3& selfPos,  const glm::vec3& selfScale,
                        const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        // AABB only — rotation ignored at this phase. Becomes SAT in Phase 4.
        const glm::vec3 aMin = selfPos  - HalfExtents       * selfScale;
        const glm::vec3 aMax = selfPos  + HalfExtents       * selfScale;
        const glm::vec3 bMin = otherPos - other.HalfExtents * otherScale;
        const glm::vec3 bMax = otherPos + other.HalfExtents * otherScale;
        return (aMin.x <= bMax.x && aMax.x >= bMin.x) &&
               (aMin.y <= bMax.y && aMax.y >= bMin.y) &&
               (aMin.z <= bMax.z && aMax.z >= bMin.z);
    }

    // Body is defined out-of-line below, after SphereShape becomes a complete
    // type — we can't call SphereShape members while it's only forward-declared.
    bool intersectsSphere(const SphereShape& other,
                          const glm::vec3& selfPos,  const glm::vec3& selfScale,
                          const glm::vec3& otherPos, const glm::vec3& otherScale) const override;

    // Box vs plane (out-of-line: needs PlaneShape complete to read its Normal).
    bool intersectsPlane(const PlaneShape& other,
                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                         const glm::vec3& otherPos, const glm::vec3& otherScale) const override;

    // --- contact-producing dispatch ---

    Contact collide(const ColliderShape& other,
                    const glm::vec3& selfPos,  const glm::vec3& selfScale,
                    const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        Contact c = other.collideBox(*this, otherPos, otherScale, selfPos, selfScale);
        c.normal = -c.normal;   // undo the A/B swap so normal points self -> other
        return c;
    }

    // AABB vs AABB: minimum-overlap axis is the minimum translation vector (MTV).
    Contact collideBox(const BoxShape& other,
                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        const glm::vec3 aMin = selfPos  - HalfExtents       * selfScale;
        const glm::vec3 aMax = selfPos  + HalfExtents       * selfScale;
        const glm::vec3 bMin = otherPos - other.HalfExtents * otherScale;
        const glm::vec3 bMax = otherPos + other.HalfExtents * otherScale;

        const glm::vec3 overlap{
            std::min(aMax.x, bMax.x) - std::max(aMin.x, bMin.x),
            std::min(aMax.y, bMax.y) - std::max(aMin.y, bMin.y),
            std::min(aMax.z, bMax.z) - std::max(aMin.z, bMin.z)
        };
        if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) return {};

        const int axis = (overlap.x < overlap.y) ? (overlap.x < overlap.z ? 0 : 2)
                                                 : (overlap.y < overlap.z ? 1 : 2);
        Contact c;
        c.hit         = true;
        c.penetration = overlap[axis];
        glm::vec3 n(0.0f);
        n[axis] = (otherPos[axis] >= selfPos[axis]) ? 1.0f : -1.0f;   // self -> other
        c.normal = n;
        c.point  = 0.5f * (glm::max(aMin, bMin) + glm::min(aMax, bMax));
        return c;
    }

    // Forwarder: defined out-of-line once SphereShape is complete.
    Contact collideSphere(const SphereShape& other,
                          const glm::vec3& selfPos,  const glm::vec3& selfScale,
                          const glm::vec3& otherPos, const glm::vec3& otherScale) const override;

    // Box vs plane contact (out-of-line: needs PlaneShape complete).
    Contact collidePlane(const PlaneShape& other,
                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                         const glm::vec3& otherPos, const glm::vec3& otherScale) const override;
};

//checks exclusively spheres, doesnt allow ellipsoids. 
class SphereShape : public ColliderShape {
public:
    float Radius = 0.5f; // local radius; world radius = Radius * scale

    explicit SphereShape(float radius = 0.5f) : Radius(radius) {}

    bool intersects(const ColliderShape& other,
                    const glm::vec3& selfPos,  const glm::vec3& selfScale,
                    const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        // Forward to the other shape's sphere-handler.
        return other.intersectsSphere(*this, otherPos, otherScale, selfPos, selfScale);
    }

    //sphere-box.
    bool intersectsBox(const BoxShape& other,
                        const glm::vec3& selfPos,  const glm::vec3& selfScale,
                        const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        // Box-sphere intersection. Rotation still ignored.
        const glm::vec3 boxHalf = other.HalfExtents * otherScale;
        const glm::vec3 boxCenter = otherPos;
        const glm::vec3 sphereCenter = selfPos;
        const float sphereRadius = Radius * selfScale.x; // uniform scale only for now

        // Find the point on the box closest to the sphere center.
        const glm::vec3 closestPoint = glm::clamp(sphereCenter, boxCenter - boxHalf, boxCenter + boxHalf);
        const float distance = glm::distance(sphereCenter, closestPoint);
        return distance < sphereRadius;
    }

    //sphere-sphere.
    bool intersectsSphere(const SphereShape& other,
                          const glm::vec3& selfPos,  const glm::vec3& selfScale,
                          const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        // Simple distance check — rotation is not relevant for spheres.
        const float distance = glm::distance(selfPos, otherPos);
        const float sumRadii = Radius * selfScale.x + other.Radius * otherScale.x;
        return distance < sumRadii;
    }

    // Sphere vs plane (out-of-line: needs PlaneShape complete).
    bool intersectsPlane(const PlaneShape& other,
                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                         const glm::vec3& otherPos, const glm::vec3& otherScale) const override;

    // --- contact-producing dispatch ---

    Contact collide(const ColliderShape& other,
                    const glm::vec3& selfPos,  const glm::vec3& selfScale,
                    const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        Contact c = other.collideSphere(*this, otherPos, otherScale, selfPos, selfScale);
        c.normal = -c.normal;   // undo the A/B swap so normal points self -> other
        return c;
    }

    // Sphere(self) vs Box(other): closest-point test. Normal points sphere -> box.
    Contact collideBox(const BoxShape& other,
                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        const glm::vec3 boxHalf   = other.HalfExtents * otherScale;
        const glm::vec3 boxCenter = otherPos;
        const glm::vec3 sphereC   = selfPos;
        const float     r         = Radius * selfScale.x;   // uniform scale only for now

        const glm::vec3 closest = glm::clamp(sphereC, boxCenter - boxHalf, boxCenter + boxHalf);
        glm::vec3 d = closest - sphereC;                    // sphere center -> box surface
        const float dist = glm::length(d);

        Contact c;
        if (dist > 1e-6f) {
            if (dist >= r) return {};                       // separated
            c.hit         = true;
            c.normal      = d / dist;                       // sphere -> box
            c.penetration = r - dist;
            c.point       = closest;
            return c;
        }
        // Sphere center is inside the box: push out along the least-penetrating face.
        const glm::vec3 pen = boxHalf - glm::abs(sphereC - boxCenter);
        const int axis = (pen.x < pen.y) ? (pen.x < pen.z ? 0 : 2)
                                         : (pen.y < pen.z ? 1 : 2);
        glm::vec3 n(0.0f);
        n[axis] = (sphereC[axis] < boxCenter[axis]) ? -1.0f : 1.0f;  // sphere -> box face
        c.hit         = true;
        c.normal      = n;
        c.penetration = pen[axis] + r;
        c.point       = sphereC;
        return c;
    }

    //sphere-sphere contact. Normal points self -> other.
    Contact collideSphere(const SphereShape& other,
                          const glm::vec3& selfPos,  const glm::vec3& selfScale,
                          const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        const glm::vec3 d = otherPos - selfPos;
        const float dist  = glm::length(d);
        const float sumR  = Radius * selfScale.x + other.Radius * otherScale.x;
        if (dist >= sumR) return {};

        Contact c;
        c.hit         = true;
        c.normal      = (dist > 1e-6f) ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
        c.penetration = sumR - dist;
        c.point       = selfPos + c.normal * (Radius * selfScale.x - c.penetration * 0.5f);
        return c;
    }

    // Sphere vs plane contact (out-of-line: needs PlaneShape complete).
    Contact collidePlane(const PlaneShape& other,
                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                         const glm::vec3& otherPos, const glm::vec3& otherScale) const override;
};

// Out-of-line: SphereShape is now complete, so calling its members is legal.
inline bool BoxShape::intersectsSphere(const SphereShape& other,
                                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                       const glm::vec3& otherPos, const glm::vec3& otherScale) const {
    // Box-sphere is symmetric, so forward to the sphere's box-handler.
    return other.intersectsBox(*this, otherPos, otherScale, selfPos, selfScale);
}

// Box(self) vs Sphere(other): forward to the sphere's box-handler, then negate the
// normal to undo the A/B swap so it points box -> sphere (self -> other).
inline Contact BoxShape::collideSphere(const SphereShape& other,
                                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                       const glm::vec3& otherPos, const glm::vec3& otherScale) const {
    Contact c = other.collideBox(*this, otherPos, otherScale, selfPos, selfScale);
    c.normal = -c.normal;
    return c;
}

// Infinite half-space: solid on the -Normal side, passing through the collider's
// position. Ideal for a ground/wall without authoring a huge box. Like the other
// shapes, the collider's rotation is ignored — Normal is in world space. Plane-vs-plane
// never reports a contact (two infinite planes aren't a solvable pair here).
class PlaneShape : public ColliderShape {
public:
    glm::vec3 Normal{0.0f, 1.0f, 0.0f}; // world up by default; normalized at test time
    bool      Infinite = true;          // true: unbounded half-space (fastest path, no lateral test)
    glm::vec2 HalfExtents{5.0f, 5.0f};  // half-size along the plane's tangent axes (used only when !Infinite)

    explicit PlaneShape(glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f),
                        bool infinite = true,
                        glm::vec2 halfExtents = glm::vec2(5.0f))
        : Normal(normal), Infinite(infinite), HalfExtents(halfExtents) {}

    // Is `worldPoint` over the (finite) plane patch? Infinite planes always pass. Builds a
    // tangent basis from Normal and clamps the in-plane projection to HalfExtents. The test
    // uses the body's CENTER (not its footprint), so a body whose center passes the edge
    // falls off — fine for a first pass; widen HalfExtents or add a wall to constrain.
    bool withinBounds(const glm::vec3& worldPoint, const glm::vec3& planePos) const {
        if (Infinite) return true;
        const glm::vec3 N   = glm::normalize(Normal);
        const glm::vec3 ref = (std::abs(N.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 T   = glm::normalize(glm::cross(ref, N));
        const glm::vec3 Bt  = glm::cross(N, T);
        const glm::vec3 d   = worldPoint - planePos;
        return std::abs(glm::dot(d, T))  <= HalfExtents.x
            && std::abs(glm::dot(d, Bt)) <= HalfExtents.y;
    }

    bool intersects(const ColliderShape& other,
                    const glm::vec3& selfPos,  const glm::vec3& selfScale,
                    const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        return other.intersectsPlane(*this, otherPos, otherScale, selfPos, selfScale);
    }
    bool intersectsBox(const BoxShape& other,
                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        return other.intersectsPlane(*this, otherPos, otherScale, selfPos, selfScale);
    }
    bool intersectsSphere(const SphereShape& other,
                          const glm::vec3& selfPos,  const glm::vec3& selfScale,
                          const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        return other.intersectsPlane(*this, otherPos, otherScale, selfPos, selfScale);
    }
    bool intersectsPlane(const PlaneShape&,
                         const glm::vec3&, const glm::vec3&,
                         const glm::vec3&, const glm::vec3&) const override { return false; }

    Contact collide(const ColliderShape& other,
                    const glm::vec3& selfPos,  const glm::vec3& selfScale,
                    const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        Contact c = other.collidePlane(*this, otherPos, otherScale, selfPos, selfScale);
        c.normal = -c.normal;
        return c;
    }
    Contact collideBox(const BoxShape& other,
                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        Contact c = other.collidePlane(*this, otherPos, otherScale, selfPos, selfScale);
        c.normal = -c.normal;
        return c;
    }
    Contact collideSphere(const SphereShape& other,
                          const glm::vec3& selfPos,  const glm::vec3& selfScale,
                          const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        Contact c = other.collidePlane(*this, otherPos, otherScale, selfPos, selfScale);
        c.normal = -c.normal;
        return c;
    }
    Contact collidePlane(const PlaneShape&,
                         const glm::vec3&, const glm::vec3&,
                         const glm::vec3&, const glm::vec3&) const override { return {}; }
};

// Box(self) vs Plane(other): an AABB's support distance along the plane normal is the
// sum of |extent_i * N_i|. The box penetrates when its center is within that distance of
// the plane. Normal points box -> plane (into the surface, i.e. -N).
inline bool BoxShape::intersectsPlane(const PlaneShape& other,
                                      const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                      const glm::vec3& otherPos, const glm::vec3& /*otherScale*/) const {
    const glm::vec3 N = glm::normalize(other.Normal);
    const glm::vec3 h = HalfExtents * selfScale;
    const float R = h.x * std::abs(N.x) + h.y * std::abs(N.y) + h.z * std::abs(N.z);
    return glm::dot(selfPos - otherPos, N) < R && other.withinBounds(selfPos, otherPos);
}
inline Contact BoxShape::collidePlane(const PlaneShape& other,
                                      const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                      const glm::vec3& otherPos, const glm::vec3& /*otherScale*/) const {
    const glm::vec3 N = glm::normalize(other.Normal);
    const glm::vec3 h = HalfExtents * selfScale;
    const float R = h.x * std::abs(N.x) + h.y * std::abs(N.y) + h.z * std::abs(N.z);
    const float s = glm::dot(selfPos - otherPos, N);
    if (s >= R || !other.withinBounds(selfPos, otherPos)) return {};
    Contact c;
    c.hit         = true;
    c.normal      = -N;          // box -> plane
    c.penetration = R - s;
    c.point       = selfPos - N * R;
    return c;
}

// Sphere(self) vs Plane(other): signed distance of the center to the plane vs radius.
// Normal points sphere -> plane (-N), so the solver pushes the sphere along +N.
inline bool SphereShape::intersectsPlane(const PlaneShape& other,
                                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                         const glm::vec3& otherPos, const glm::vec3& /*otherScale*/) const {
    const glm::vec3 N = glm::normalize(other.Normal);
    const float r = Radius * selfScale.x;
    return glm::dot(selfPos - otherPos, N) < r && other.withinBounds(selfPos, otherPos);
}
inline Contact SphereShape::collidePlane(const PlaneShape& other,
                                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                         const glm::vec3& otherPos, const glm::vec3& /*otherScale*/) const {
    const glm::vec3 N = glm::normalize(other.Normal);
    const float r = Radius * selfScale.x;
    const float s = glm::dot(selfPos - otherPos, N);
    if (s >= r || !other.withinBounds(selfPos, otherPos)) return {};
    Contact c;
    c.hit         = true;
    c.normal      = -N;          // sphere -> plane
    c.penetration = r - s;
    c.point       = selfPos - N * r;
    return c;
}


// -----------------------------------------------------------------------------
// ColliderComponent3D
// -----------------------------------------------------------------------------


class ColliderComponent3D : public SpatialComponent3D {
public:
    using Callback = std::function<void(ColliderComponent3D* other)>;

    // Filtering: pair fires only if (a.Layer & b.Mask) && (b.Layer & a.Mask).
    uint32_t Layer = 0x1;
    uint32_t Mask  = 0xFFFFFFFF;

    std::unique_ptr<ColliderShape> Shape;

    Callback OnEnter;
    Callback OnStay;
    Callback OnExit;

    ColliderComponent3D() = default;
    explicit ColliderComponent3D(std::unique_ptr<ColliderShape> shape) : Shape(std::move(shape)) {}

    // JSON: { "type": "Collider", "layer": 1, "mask": 4294967295,
    //         "shape": { "kind": "box", "halfExtents": [x,y,z] } }   // or "sphere" + "radius"
    void deserialize(const nlohmann::json& j, LoadContext&) override {
        if (j.contains("layer")) Layer = j["layer"].get<uint32_t>();
        if (j.contains("mask"))  Mask  = j["mask"].get<uint32_t>();
        if (!j.contains("shape")) return;

        const nlohmann::json& s = j["shape"];
        const std::string kind = s.value("kind", std::string{"box"});
        if (kind == "box") {
            glm::vec3 he = s.contains("halfExtents") ? jsonVec3(s["halfExtents"], glm::vec3(0.5f))
                                                     : glm::vec3(0.5f);
            Shape = std::make_unique<BoxShape>(he);
        } else if (kind == "sphere") {
            Shape = std::make_unique<SphereShape>(s.value("radius", 0.5f));
        } else if (kind == "plane") {
            glm::vec3 n = s.contains("normal") ? jsonVec3(s["normal"], glm::vec3(0.0f, 1.0f, 0.0f))
                                               : glm::vec3(0.0f, 1.0f, 0.0f);
            const bool infinite = s.value("infinite", true);
            glm::vec2 he{5.0f, 5.0f};
            if (s.contains("halfExtents") && s["halfExtents"].is_array() && s["halfExtents"].size() >= 2)
                he = { s["halfExtents"][0].get<float>(), s["halfExtents"][1].get<float>() };
            Shape = std::make_unique<PlaneShape>(n, infinite, he);
        } else {
            std::cerr << "[ColliderComponent3D] unknown shape kind: " << kind << "\n";
        }
    }

    void start() override {
        SpatialComponent3D::start();
        if (!Shape) std::cerr << "Warning: ColliderComponent3D on " << owner->Name << " has no shape.\n";
        AllColliders().push_back(this);
    }

    ~ColliderComponent3D() override {
        auto& list = AllColliders();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    // Read-only access to the live-collider registry for the physics solver
    // (RigidBodyComponent3D::stepWorld), which sweeps colliders for contacts without
    // maintaining a parallel list. The trigger path keeps using the private one.
    static const std::vector<ColliderComponent3D*>& colliders() { return AllColliders(); }

    // Public wrapper over the layer/mask filter so the solver can reuse the same rule.
    static bool masksOverlap(const ColliderComponent3D* a, const ColliderComponent3D* b) {
        return passesMask(a, b);
    }

    // Call once per frame (e.g. from main loop). Naive O(N^2). Broadphase later.
    // TODO: optimize call, only check the active components on the same layer. No need to check all the colliders.
    static void tickWorld() {
        auto& list = AllColliders();
        //std::cout << list.size() << std::endl;
        for (auto* c : list) {
            c->thisFrame.clear();
            const glm::vec3& p = c->position();
            //std::cout << "[Collider] pos=(" << p.x << ", " << p.y << ", " << p.z << ")\n";
        }

        // for (size_t i = 0; i < list.size(); ++i) {
        //     for (size_t j = i + 1; j < list.size(); ++j) {
        //         auto* a = list[i];
        //         auto* b = list[j];
        //         if (!passesMask(a, b)) continue;
        //         if (!a->Shape || !b->Shape) continue;

        //         if (a->Shape->intersects(*b->Shape,
        //                                  a->position(), a->scale(),
        //                                  b->position(), b->scale())) {
        //                                     //std::cout << "Collision Enter " << std::endl;
        //             a->thisFrame.insert(b);
        //             b->thisFrame.insert(a);
        //         }
        //     }
        // }

        //triggers parallelization at over 256 colliders.
        #pragma omp parallel for schedule(guided) if(list.size() > 256)
        for (size_t i = 0; i < list.size(); ++i) {
            auto* a = list[i];
            a->thisFrame.clear();
            if (!a->Shape) continue;
            for (size_t j = 0; j < list.size(); ++j) {
                if (j == i) continue;
                auto* b = list[j];
                if (!passesMask(a, b) || !b->Shape) continue;
                if (a->Shape->intersects(*b->Shape, a->position(), a->scale(),
                                                    b->position(), b->scale()))
                    a->thisFrame.insert(b);
            }
        }


        // Diff against previous frame to fire enter/stay/exit.
        for (auto* c : list) {
            for (auto* other : c->thisFrame) {
                if (c->lastFrame.count(other)) {
                    if (c->OnStay) c->OnStay(other);
                } else {
                    if (c->OnEnter) c->OnEnter(other);
                }
            }
            for (auto* other : c->lastFrame) {
                if (!c->thisFrame.count(other)) {
                    if (c->OnExit) c->OnExit(other);
                }
            }
            c->lastFrame = c->thisFrame;
        }
    }

private:
    std::unordered_set<ColliderComponent3D*> thisFrame;
    std::unordered_set<ColliderComponent3D*> lastFrame;

    static std::vector<ColliderComponent3D*>& AllColliders() {
        static std::vector<ColliderComponent3D*> v;
        return v;
    }

    static bool passesMask(const ColliderComponent3D* a, const ColliderComponent3D* b) {
        return (a->Layer & b->Mask) != 0 && (b->Layer & a->Mask) != 0;
    }
};

REGISTER_COMPONENT(ColliderComponent3D, "Collider")
