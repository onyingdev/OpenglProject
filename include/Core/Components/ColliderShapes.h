#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Core/Components/RotationUtil.h"   // shared eulerToQuat
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// -----------------------------------------------------------------------------
// Collision shape hierarchy (pose-free, render-free, component-free)
// -----------------------------------------------------------------------------
// Pure geometry shared by the physics ColliderComponent3D and the gameplay
// HurtBoxComponent3D. Each concrete shape owns its parameters (half-extents,
// radius, ...); the world transform is supplied at test time so the shape itself
// stays stateless. Two parallel double-dispatch families:
//   - intersects()  -> bool        (cheap overlap test; the trigger/hurtbox path)
//   - collide()     -> Contact     (normal + penetration; the physics-solver path)
// kind() lets non-virtual consumers (e.g. debug-draw) dispatch without RTTI.

class BoxShape;
class SphereShape;
class PlaneShape;
class OBBShape;

// Layer/mask filter shared by every collision sweep: a pair interacts only if each
// side's layer is in the other's mask.
inline bool layerMaskOverlap(uint32_t layerA, uint32_t maskA, uint32_t layerB, uint32_t maskB) {
    return (layerA & maskB) != 0 && (layerB & maskA) != 0;
}

// Tag for type-switching without RTTI (debug-draw, future shape-specific paths).
// (eulerToQuat lives in Core/Components/RotationUtil.h, shared with the Transform layer.)
enum class ShapeKind { Box, Sphere, Plane, OBB, Ball };

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

// World-space axis-aligned bounds of a shape, for the broadphase grid. `infinite` flags shapes with
// no finite box (an infinite plane) — those can't be binned into cells (they'd span every one), so
// the broadphase keeps them in a separate list and tests every finite body against them directly.
struct AABBox {
    glm::vec3 min{0.0f}, max{0.0f};
    bool      infinite = false;
};

class ColliderShape {
public:
    virtual ~ColliderShape() = default;

    // Concrete type, for non-virtual consumers (debug-draw dispatch).
    virtual ShapeKind kind() const = 0;

    // Set the shape's world orientation from the owner Transform each sweep. OBB stores it;
    // Plane rotates its normal; box/sphere are rotation-invariant and ignore it.
    virtual void setOrientation(const glm::quat& /*world*/) {}

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

    virtual bool intersectsOBB(const OBBShape& other,
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

    virtual Contact collideOBB(const OBBShape& other,
                               const glm::vec3& selfPos,  const glm::vec3& selfScale,
                               const glm::vec3& otherPos, const glm::vec3& otherScale) const = 0;

    // Contact MANIFOLD: write up to maxOut contact points into out[], return the count. The
    // default is the single collide() contact (fine for spheres / point-ish contacts); box &
    // OBB override it for the resting-on-a-plane case, emitting one contact per penetrating
    // corner so a flat box is held by a balanced set of points (no single-point rocking/jitter).
    virtual int manifold(const ColliderShape& other,
                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                         const glm::vec3& otherPos, const glm::vec3& otherScale,
                         Contact* out, int maxOut) const {
        if (maxOut < 1) return 0;
        const Contact c = collide(other, selfPos, selfScale, otherPos, otherScale);
        if (!c.hit) return 0;
        out[0] = c;
        return 1;
    }

    // World AABB for the broadphase grid. Default: report infinite (safe fallback — an unrecognized
    // shape is tested against everything rather than wrongly culled). Concrete shapes override.
    virtual AABBox worldAABB(const glm::vec3& pos, const glm::vec3& /*scale*/) const {
        return { pos, pos, true };
    }
};

class BoxShape : public ColliderShape {
public:
    glm::vec3 HalfExtents{0.5f}; // local half-size; world size = HalfExtents * scale

    explicit BoxShape(glm::vec3 halfExtents = glm::vec3(0.5f)) : HalfExtents(halfExtents) {}

    ShapeKind kind() const override { return ShapeKind::Box; }

    AABBox worldAABB(const glm::vec3& pos, const glm::vec3& scale) const override {
        const glm::vec3 h = HalfExtents * scale;
        return { pos - h, pos + h, false };
    }

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
        // AABB only — rotation ignored at this phase. Becomes SAT once OBB lands.
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

    // Box vs OBB (out-of-line forwarder: needs OBBShape complete).
    bool intersectsOBB(const OBBShape& other,
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

    // Box vs OBB contact (out-of-line forwarder: needs OBBShape complete).
    Contact collideOBB(const OBBShape& other,
                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override;

    // Manifold (out-of-line: needs PlaneShape). AABB-vs-plane emits a corner manifold.
    int manifold(const ColliderShape& other,
                 const glm::vec3& selfPos,  const glm::vec3& selfScale,
                 const glm::vec3& otherPos, const glm::vec3& otherScale,
                 Contact* out, int maxOut) const override;
};

//checks exclusively spheres, doesnt allow ellipsoids.
class SphereShape : public ColliderShape {
public:
    float Radius = 0.5f; // local radius; world radius = Radius * scale

    explicit SphereShape(float radius = 0.5f) : Radius(radius) {}

    ShapeKind kind() const override { return ShapeKind::Sphere; }

    AABBox worldAABB(const glm::vec3& pos, const glm::vec3& scale) const override {
        const float r = Radius * scale.x;   // uniform scale only for now (matches collide())
        return { pos - glm::vec3(r), pos + glm::vec3(r), false };
    }

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

    // Sphere vs OBB (out-of-line forwarder: needs OBBShape complete).
    bool intersectsOBB(const OBBShape& other,
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

    // Sphere vs OBB contact (out-of-line forwarder: needs OBBShape complete).
    Contact collideOBB(const OBBShape& other,
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
    glm::vec3 Normal{0.0f, 1.0f, 0.0f};      // WORLD normal used by the math (synced from the Transform)
    bool      Infinite = true;               // true: unbounded half-space (fastest path, no lateral test)
    glm::vec2 HalfExtents{5.0f, 5.0f};       // half-size along the plane's tangent axes (used only when !Infinite)
    glm::vec3 LocalNormal{0.0f, 1.0f, 0.0f}; // authored normal; Normal = ownerRotation * LocalNormal
    bool      OneSided = true;               // true: only collide bodies on the +N side (a body whose
                                             // center is below the plane passes through, like a game
                                             // ground/one-way platform). false: two-sided half-space.

    explicit PlaneShape(glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f),
                        bool infinite = true,
                        glm::vec2 halfExtents = glm::vec2(5.0f),
                        bool oneSided = true)
        : Normal(normal), Infinite(infinite), HalfExtents(halfExtents), LocalNormal(normal), OneSided(oneSided) {}

    ShapeKind kind() const override { return ShapeKind::Plane; }

    // A plane (finite or infinite) is treated as unbounded for the grid: it never gets binned into
    // cells — the broadphase tests it against every finite body via the unbounded list.
    AABBox worldAABB(const glm::vec3&, const glm::vec3&) const override { return { glm::vec3(0.0f), glm::vec3(0.0f), true }; }

    // Orient the world normal by the owner Transform's rotation (math normalizes it anyway).
    void setOrientation(const glm::quat& q) override { Normal = q * LocalNormal; }

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

    // Orthonormal in-plane basis from Normal (used by withinBounds and debug-draw).
    void tangentBasis(glm::vec3& outT, glm::vec3& outB) const {
        const glm::vec3 N   = glm::normalize(Normal);
        const glm::vec3 ref = (std::abs(N.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        outT = glm::normalize(glm::cross(ref, N));
        outB = glm::cross(N, outT);
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

    // Plane vs OBB (out-of-line forwarder: needs OBBShape complete).
    bool intersectsOBB(const OBBShape& other,
                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override;

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

    // Plane vs OBB contact (out-of-line forwarder: needs OBBShape complete).
    Contact collideOBB(const OBBShape& other,
                       const glm::vec3& selfPos,  const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override;
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
    const float s = glm::dot(selfPos - otherPos, N);
    if (other.OneSided && s < 0.0f) return false;        // center below a one-sided plane → no contact
    return s < R && other.withinBounds(selfPos, otherPos);
}
inline Contact BoxShape::collidePlane(const PlaneShape& other,
                                      const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                      const glm::vec3& otherPos, const glm::vec3& /*otherScale*/) const {
    const glm::vec3 N = glm::normalize(other.Normal);
    const glm::vec3 h = HalfExtents * selfScale;
    const float R = h.x * std::abs(N.x) + h.y * std::abs(N.y) + h.z * std::abs(N.z);
    const float s = glm::dot(selfPos - otherPos, N);
    if (s >= R || !other.withinBounds(selfPos, otherPos)) return {};
    if (other.OneSided && s < 0.0f) return {};           // center below a one-sided plane → no contact
    Contact c;
    c.hit         = true;
    c.normal      = -N;          // box -> plane
    c.penetration = R - s;
    // Deepest corner (support vertex in -N), not the center — gives a real torque arm so a
    // tilted box tumbles instead of receiving a centered (torque-free) impulse.
    c.point       = selfPos - glm::sign(N) * h;
    return c;
}

// Sphere(self) vs Plane(other): signed distance of the center to the plane vs radius.
// Normal points sphere -> plane (-N), so the solver pushes the sphere along +N.
inline bool SphereShape::intersectsPlane(const PlaneShape& other,
                                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                         const glm::vec3& otherPos, const glm::vec3& /*otherScale*/) const {
    const glm::vec3 N = glm::normalize(other.Normal);
    const float r = Radius * selfScale.x;
    const float s = glm::dot(selfPos - otherPos, N);
    if (other.OneSided && s < 0.0f) return false;        // center below a one-sided plane → no contact
    return s < r && other.withinBounds(selfPos, otherPos);
}
inline Contact SphereShape::collidePlane(const PlaneShape& other,
                                         const glm::vec3& selfPos,  const glm::vec3& selfScale,
                                         const glm::vec3& otherPos, const glm::vec3& /*otherScale*/) const {
    const glm::vec3 N = glm::normalize(other.Normal);
    const float r = Radius * selfScale.x;
    const float s = glm::dot(selfPos - otherPos, N);
    if (s >= r || !other.withinBounds(selfPos, otherPos)) return {};
    if (other.OneSided && s < 0.0f) return {};           // center below a one-sided plane → no contact
    Contact c;
    c.hit         = true;
    c.normal      = -N;          // sphere -> plane
    c.penetration = r - s;
    c.point       = selfPos - N * r;
    return c;
}

// -----------------------------------------------------------------------------
// OBB (oriented bounding box) + Separating Axis Theorem
// -----------------------------------------------------------------------------

// SAT between two boxes given world centers, unit axes, and world half-extents. Returns a
// Contact whose normal points A -> B (least-penetration axis = MTV). Tests 15 axes: A's 3
// faces, B's 3 faces, 9 edge cross-products; near-zero cross axes (parallel edges) are
// skipped. An AABB is just a box with identity axes, so this serves OBB-vs-AABB too.
// Richer SAT result: besides the MTV (normal A->B + penetration) it reports WHICH axis won —
// a face of A, a face of B, or an edge-edge cross — and the axis index/indices. The contact
// manifold builder needs this to know which face to clip (or that it's an edge-edge point).
struct SatResult {
    bool      hit         = false;
    glm::vec3 normal{0.0f};        // A -> B (MTV direction)
    float     penetration = 0.0f;
    int       type        = 0;     // 0 = A face, 1 = B face, 2 = edge-edge
    int       aIndex      = -1;    // A face/edge axis (0..2), or -1
    int       bIndex      = -1;    // B face/edge axis (0..2), or -1
};

// Full 15-axis SAT (A's 3 faces, B's 3 faces, 9 edge cross-products). Faces are BIASED over
// edges on near-ties: a flat box resting on a flat box has a face contact and an edge contact at
// nearly equal penetration, and flip-flopping between them frame-to-frame is itself a jitter
// source — so an edge axis only wins if it's clearly shallower. An AABB is a box with identity
// axes, so this serves OBB-vs-AABB too.
inline SatResult satBoxesEx(const glm::vec3& ca, const glm::vec3 ax[3], const glm::vec3& ha,
                            const glm::vec3& cb, const glm::vec3 bx[3], const glm::vec3& hb) {
    const glm::vec3 t = cb - ca;            // A center -> B center
    SatResult r;
    bool separated = false;

    // Penetration along (possibly unnormalized) axis L. Returns false on a degenerate axis or
    // on a separating axis (sets `separated`). Writes the oriented (A->B) unit axis + depth.
    auto axisPen = [&](glm::vec3 L, glm::vec3& outAxis, float& outPen) -> bool {
        const float len = glm::length(L);
        if (len < 1e-6f) return false;      // degenerate (parallel edges) — covered by face axes
        L /= len;
        const float ra = ha.x*std::abs(glm::dot(ax[0],L)) + ha.y*std::abs(glm::dot(ax[1],L)) + ha.z*std::abs(glm::dot(ax[2],L));
        const float rb = hb.x*std::abs(glm::dot(bx[0],L)) + hb.y*std::abs(glm::dot(bx[1],L)) + hb.z*std::abs(glm::dot(bx[2],L));
        const float pen = (ra + rb) - std::abs(glm::dot(t, L));
        if (pen < 0.0f) { separated = true; return false; }
        outAxis = (glm::dot(t, L) < 0.0f) ? -L : L;
        outPen  = pen;
        return true;
    };

    // Faces first.
    float faceMin = std::numeric_limits<float>::max();
    glm::vec3 faceAxis(0.0f); int faceType = 0, faceA = -1, faceB = -1;
    for (int i = 0; i < 3 && !separated; ++i) { glm::vec3 L; float p; if (axisPen(ax[i], L, p) && p < faceMin) { faceMin = p; faceAxis = L; faceType = 0; faceA = i; faceB = -1; } }
    for (int i = 0; i < 3 && !separated; ++i) { glm::vec3 L; float p; if (axisPen(bx[i], L, p) && p < faceMin) { faceMin = p; faceAxis = L; faceType = 1; faceA = -1; faceB = i; } }
    if (separated) return r;                 // hit stays false

    // Then edges.
    float edgeMin = std::numeric_limits<float>::max();
    glm::vec3 edgeAxis(0.0f); int edgeA = -1, edgeB = -1;
    for (int i = 0; i < 3 && !separated; ++i)
        for (int j = 0; j < 3 && !separated; ++j) { glm::vec3 L; float p; if (axisPen(glm::cross(ax[i], bx[j]), L, p) && p < edgeMin) { edgeMin = p; edgeAxis = L; edgeA = i; edgeB = j; } }
    if (separated) return r;

    r.hit = true;
    // Bias: edge only wins if clearly shallower than the best face (relative + absolute slack).
    constexpr float kRelTol = 0.95f, kAbsTol = 0.005f;
    if (edgeMin < faceMin * kRelTol - kAbsTol) {
        r.normal = edgeAxis; r.penetration = edgeMin; r.type = 2; r.aIndex = edgeA; r.bIndex = edgeB;
    } else {
        r.normal = faceAxis; r.penetration = faceMin; r.type = faceType; r.aIndex = faceA; r.bIndex = faceB;
    }
    return r;
}

// Single-point form for collide()/intersects() (which only read hit/normal/penetration). Keeps the
// old "midpoint between centers" contact point — those callers ignore the point anyway.
inline Contact satBoxes(const glm::vec3& ca, const glm::vec3 ax[3], const glm::vec3& ha,
                        const glm::vec3& cb, const glm::vec3 bx[3], const glm::vec3& hb) {
    const SatResult s = satBoxesEx(ca, ax, ha, cb, bx, hb);
    if (!s.hit) return {};
    Contact c;
    c.hit         = true;
    c.normal      = s.normal;               // A -> B
    c.penetration = s.penetration;
    c.point       = ca + 0.5f * (cb - ca);  // approximate (single-point callers ignore the point)
    return c;
}

// Box-vs-plane contact MANIFOLD: one contact per box corner penetrating the plane (normal
// box -> plane = -N). Shared by AABB (identity axes) and OBB (oriented axes). A flat box rests
// on 4 balanced corners → the per-corner normal impulses cancel any rocking torque, killing the
// single-point jitter. C = box center, a[3] = unit world axes, h = world half-extents.
inline int boxPlaneManifold(const glm::vec3& C, const glm::vec3 a[3], const glm::vec3& h,
                            const PlaneShape& plane, const glm::vec3& P, Contact* out, int maxOut) {
    const glm::vec3 N = glm::normalize(plane.Normal);
    if (plane.OneSided && glm::dot(C - P, N) < 0.0f) return 0;   // box center below a one-sided plane → skip
    int count = 0;
    for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
    for (int sz = -1; sz <= 1; sz += 2) {
        const glm::vec3 corner = C + (float)sx * h.x * a[0]
                                   + (float)sy * h.y * a[1]
                                   + (float)sz * h.z * a[2];
        const float d = glm::dot(corner - P, N);                 // signed distance to plane
        if (d < 0.0f && plane.withinBounds(corner, P)) {         // corner is below/in the plane
            if (count >= maxOut) return count;
            out[count].hit         = true;
            out[count].normal      = -N;
            out[count].penetration = -d;
            out[count].point       = corner;
            ++count;
        }
    }
    return count;
}

// Closest points between two segments [a0,a1] and [b0,b1] (Ericson, Real-Time Collision
// Detection). Writes the closest point on each; used for the edge-edge contact point.
inline void closestSegSeg(const glm::vec3& a0, const glm::vec3& a1,
                          const glm::vec3& b0, const glm::vec3& b1,
                          glm::vec3& c1, glm::vec3& c2) {
    const glm::vec3 d1 = a1 - a0, d2 = b1 - b0, r = a0 - b0;
    const float A = glm::dot(d1, d1), E = glm::dot(d2, d2), F = glm::dot(d2, r);
    constexpr float EPS = 1e-8f;
    float s, t;
    if (A <= EPS && E <= EPS) { s = 0.0f; t = 0.0f; }
    else if (A <= EPS)        { s = 0.0f; t = glm::clamp(F / E, 0.0f, 1.0f); }
    else {
        const float C = glm::dot(d1, r);
        if (E <= EPS) { t = 0.0f; s = glm::clamp(-C / A, 0.0f, 1.0f); }
        else {
            const float B = glm::dot(d1, d2);
            const float denom = A * E - B * B;
            s = (denom != 0.0f) ? glm::clamp((B * F - C * E) / denom, 0.0f, 1.0f) : 0.0f;
            t = (B * s + F) / E;
            if      (t < 0.0f) { t = 0.0f; s = glm::clamp(-C / A,        0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = glm::clamp((B - C) / A,   0.0f, 1.0f); }
        }
    }
    c1 = a0 + d1 * s;
    c2 = b0 + d2 * t;
}

// Oriented box-vs-box contact MANIFOLD via Sutherland-Hodgman face clipping. Produces the balanced
// multi-point contact set a stack needs (single-point SAT rocks, like a box on a plane did before
// boxPlaneManifold). Normal is A->B for every emitted contact. Handles AABB (identity axes), OBB,
// and the mix. c*/.*x[3]/h* are world center / unit axes / world half-extents.
inline int boxBoxManifold(const glm::vec3& ca, const glm::vec3 ax[3], const glm::vec3& ha,
                          const glm::vec3& cb, const glm::vec3 bx[3], const glm::vec3& hb,
                          Contact* out, int maxOut) {
    if (maxOut < 1) return 0;
    const SatResult s = satBoxesEx(ca, ax, ha, cb, bx, hb);
    if (!s.hit) return 0;

    // Edge-edge: a single contact at the closest points of the two involved edges. Each edge runs
    // along its box's contact axis, offset along the other two axes toward the other box.
    if (s.type == 2) {
        const int ea = s.aIndex, eb = s.bIndex;
        glm::vec3 pA = ca, pB = cb;
        for (int k = 0; k < 3; ++k) {
            if (k != ea) pA += (glm::dot(ax[k], s.normal) >= 0.0f ?  ha[k] : -ha[k]) * ax[k]; // toward B (+normal)
            if (k != eb) pB += (glm::dot(bx[k], s.normal) >= 0.0f ? -hb[k] :  hb[k]) * bx[k]; // toward A (-normal)
        }
        glm::vec3 c1, c2;
        closestSegSeg(pA - ha[ea] * ax[ea], pA + ha[ea] * ax[ea],
                      pB - hb[eb] * bx[eb], pB + hb[eb] * bx[eb], c1, c2);
        out[0].hit         = true;
        out[0].normal      = s.normal;
        out[0].penetration = s.penetration;
        out[0].point       = 0.5f * (c1 + c2);
        return 1;
    }

    // Face contact. Reference = the box whose face won; nR is the reference face's outward normal
    // (toward the incident box). The emitted contact normal is always A->B (= s.normal).
    const bool refIsA = (s.type == 0);
    const glm::vec3  cR  = refIsA ? ca : cb;
    const glm::vec3* R   = refIsA ? ax : bx;
    const glm::vec3  hR  = refIsA ? ha : hb;
    const int        ri  = refIsA ? s.aIndex : s.bIndex;
    const glm::vec3  nR  = refIsA ? s.normal : -s.normal;     // reference outward, toward incident

    const glm::vec3  cI  = refIsA ? cb : ca;
    const glm::vec3* I   = refIsA ? bx : ax;
    const glm::vec3  hI  = refIsA ? hb : ha;

    // Reference face: center + the two in-face (tangent) axes with their half-widths.
    const int ru = (ri + 1) % 3, rw = (ri + 2) % 3;
    const glm::vec3 refC = cR + hR[ri] * nR;
    const glm::vec3 u = R[ru]; const float hu = hR[ru];
    const glm::vec3 w = R[rw]; const float hw = hR[rw];

    // Incident face: the incident-box face whose outward normal is most anti-parallel to nR.
    int   ik = 0; float isgn = 1.0f, best = 2.0f;
    for (int k = 0; k < 3; ++k) {
        const float dp = glm::dot(I[k], nR);
        if ( dp < best) { best =  dp; ik = k; isgn =  1.0f; }   // +I[k] faces toward -nR
        if (-dp < best) { best = -dp; ik = k; isgn = -1.0f; }   // -I[k] faces toward -nR
    }
    const int iu = (ik + 1) % 3, iw = (ik + 2) % 3;
    const glm::vec3 incC = cI + isgn * hI[ik] * I[ik];
    // The 4 incident-face vertices.
    glm::vec3 poly[8];
    int n = 0;
    for (int su = -1; su <= 1; su += 2)
        for (int sw = -1; sw <= 1; sw += 2)
            poly[n++] = incC + (float)su * hI[iu] * I[iu] + (float)sw * hI[iw] * I[iw];

    // Clip the incident polygon against the 4 side planes of the reference face (keep inside).
    auto clip = [](glm::vec3* in, int inN, const glm::vec3& planePt, const glm::vec3& outN,
                   glm::vec3* outBuf) -> int {
        int m = 0;
        for (int i = 0; i < inN; ++i) {
            const glm::vec3& cur = in[i];
            const glm::vec3& nxt = in[(i + 1) % inN];
            const float dc = glm::dot(cur - planePt, outN);
            const float dn = glm::dot(nxt - planePt, outN);
            if (dc <= 0.0f) outBuf[m++] = cur;
            if ((dc <= 0.0f) != (dn <= 0.0f)) {
                const float tt = dc / (dc - dn);
                outBuf[m++] = cur + tt * (nxt - cur);
            }
        }
        return m;
    };
    glm::vec3 tmp[8];
    n = clip(poly, n, refC + hu * u,  u, tmp);   for (int i = 0; i < n; ++i) poly[i] = tmp[i];
    n = clip(poly, n, refC - hu * u, -u, tmp);   for (int i = 0; i < n; ++i) poly[i] = tmp[i];
    n = clip(poly, n, refC + hw * w,  w, tmp);   for (int i = 0; i < n; ++i) poly[i] = tmp[i];
    n = clip(poly, n, refC - hw * w, -w, tmp);   for (int i = 0; i < n; ++i) poly[i] = tmp[i];

    // Keep clipped points at/under the reference face; each is a contact (depth along nR).
    int count = 0;
    constexpr float kClipTol = 0.01f;            // keep just-touching points too (stable manifold)
    for (int i = 0; i < n && count < maxOut; ++i) {
        const float d = glm::dot(refC - poly[i], nR);   // >0 == below the face == penetrating
        if (d <= -kClipTol) continue;
        out[count].hit         = true;
        out[count].normal      = s.normal;       // A -> B
        out[count].penetration = std::max(d, 0.0f);
        out[count].point       = poly[i];
        ++count;
    }

    // Degenerate fallback (shouldn't trigger for a real face overlap): one midpoint contact.
    if (count == 0) {
        out[0].hit         = true;
        out[0].normal      = s.normal;
        out[0].penetration = s.penetration;
        out[0].point       = ca + 0.5f * (cb - ca);
        return 1;
    }
    return count;
}

// BoxShape::manifold is defined out-of-line below OBBShape (it routes box/OBB pairs to
// boxBoxManifold, which needs the OBBShape type complete).

// Oriented box. Unlike BoxShape (AABB), it carries a WORLD orientation refreshed from the
// owner Transform by ColliderComponent3D::syncShape() each frame. SAT-resolved (~20-50x an
// AABB test), so it's opt-in for rotational accuracy; BoxShape stays the cheap default.
class OBBShape : public ColliderShape {
public:
    glm::vec3 HalfExtents{0.5f};                 // local half-size; world = HalfExtents * scale
    glm::quat Orientation{1.0f, 0.0f, 0.0f, 0.0f}; // world orientation (synced from the Transform)

    explicit OBBShape(glm::vec3 halfExtents = glm::vec3(0.5f)) : HalfExtents(halfExtents) {}

    ShapeKind kind() const override { return ShapeKind::OBB; }

    void setOrientation(const glm::quat& q) override { Orientation = q; }

    // AABB of an oriented box: half-width on each world axis is the sum of the rotated extents'
    // contributions, |aᵢ·ê| · hᵢ (the same projection collidePlane uses, done per world axis).
    AABBox worldAABB(const glm::vec3& pos, const glm::vec3& scale) const override {
        glm::vec3 a[3]; axes(a);
        const glm::vec3 h = HalfExtents * scale;
        const glm::vec3 r = glm::abs(a[0]) * h.x + glm::abs(a[1]) * h.y + glm::abs(a[2]) * h.z;
        return { pos - r, pos + r, false };
    }

    // World axes (unit columns of the rotation matrix).
    void axes(glm::vec3 out[3]) const {
        const glm::mat3 R = glm::mat3_cast(Orientation);
        out[0] = R[0]; out[1] = R[1]; out[2] = R[2];
    }

    // Oriented box vs plane/box/OBB emits a multi-point manifold (balanced contacts → no resting
    // jitter, and stable stacking). Sphere/ball and other kinds keep the single-point fallback.
    int manifold(const ColliderShape& other,
                 const glm::vec3& selfPos,  const glm::vec3& selfScale,
                 const glm::vec3& otherPos, const glm::vec3& otherScale,
                 Contact* out, int maxOut) const override {
        glm::vec3 a[3]; axes(a);
        if (other.kind() == ShapeKind::Plane) {
            return boxPlaneManifold(selfPos, a, HalfExtents * selfScale,
                                    static_cast<const PlaneShape&>(other), otherPos, out, maxOut);
        }
        if (other.kind() == ShapeKind::Box) {
            const glm::vec3 b[3] = { glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) };
            const auto& o = static_cast<const BoxShape&>(other);
            return boxBoxManifold(selfPos, a, HalfExtents * selfScale,
                                  otherPos, b, o.HalfExtents * otherScale, out, maxOut);
        }
        if (other.kind() == ShapeKind::OBB) {
            const auto& o = static_cast<const OBBShape&>(other);
            glm::vec3 b[3]; o.axes(b);
            return boxBoxManifold(selfPos, a, HalfExtents * selfScale,
                                  otherPos, b, o.HalfExtents * otherScale, out, maxOut);
        }
        if (maxOut < 1) return 0;
        const Contact c = collide(other, selfPos, selfScale, otherPos, otherScale);
        if (!c.hit) return 0;
        out[0] = c;
        return 1;
    }

    // --- boolean dispatch (each is the boolean form of the matching contact test) ---
    bool intersects(const ColliderShape& other,
                    const glm::vec3& selfPos, const glm::vec3& selfScale,
                    const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        return other.intersectsOBB(*this, otherPos, otherScale, selfPos, selfScale);
    }
    bool intersectsBox(const BoxShape& other,
                       const glm::vec3& selfPos, const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        return collideBox(other, selfPos, selfScale, otherPos, otherScale).hit;
    }
    bool intersectsSphere(const SphereShape& other,
                          const glm::vec3& selfPos, const glm::vec3& selfScale,
                          const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        return collideSphere(other, selfPos, selfScale, otherPos, otherScale).hit;
    }
    bool intersectsPlane(const PlaneShape& other,
                         const glm::vec3& selfPos, const glm::vec3& selfScale,
                         const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        return collidePlane(other, selfPos, selfScale, otherPos, otherScale).hit;
    }
    bool intersectsOBB(const OBBShape& other,
                       const glm::vec3& selfPos, const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        return collideOBB(other, selfPos, selfScale, otherPos, otherScale).hit;
    }

    // --- contact dispatch (OBB owns every OBB-pair base case) ---
    Contact collide(const ColliderShape& other,
                    const glm::vec3& selfPos, const glm::vec3& selfScale,
                    const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        Contact c = other.collideOBB(*this, otherPos, otherScale, selfPos, selfScale);
        c.normal = -c.normal;
        return c;
    }

    // OBB(self) vs AABB box(other): SAT with the box as an identity-oriented OBB.
    Contact collideBox(const BoxShape& other,
                       const glm::vec3& selfPos, const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        glm::vec3 a[3]; axes(a);
        const glm::vec3 b[3] = { glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) };
        return satBoxes(selfPos, a, HalfExtents * selfScale, otherPos, b, other.HalfExtents * otherScale);
    }

    // OBB(self) vs OBB(other): full 15-axis SAT.
    Contact collideOBB(const OBBShape& other,
                       const glm::vec3& selfPos, const glm::vec3& selfScale,
                       const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        glm::vec3 a[3]; axes(a);
        glm::vec3 b[3]; other.axes(b);
        return satBoxes(selfPos, a, HalfExtents * selfScale, otherPos, b, other.HalfExtents * otherScale);
    }

    // OBB(self) vs Sphere(other): closest point in the box's local frame. Normal OBB -> sphere.
    Contact collideSphere(const SphereShape& other,
                          const glm::vec3& selfPos, const glm::vec3& selfScale,
                          const glm::vec3& otherPos, const glm::vec3& otherScale) const override {
        const glm::vec3 h   = HalfExtents * selfScale;
        const float     r   = other.Radius * otherScale.x;
        const glm::vec3 lc  = glm::conjugate(Orientation) * (otherPos - selfPos);  // sphere center, box-local
        const glm::vec3 cl  = glm::clamp(lc, -h, h);
        glm::vec3 d         = lc - cl;                       // box surface -> sphere (local)
        const float dist    = glm::length(d);

        Contact c;
        if (dist > 1e-6f) {
            if (dist >= r) return {};
            c.hit         = true;
            c.normal      = Orientation * (d / dist);        // box -> sphere, back to world
            c.penetration = r - dist;
            c.point       = selfPos + Orientation * cl;
            return c;
        }
        // Sphere center inside the box: push out along the least-penetrating local face.
        const glm::vec3 pen = h - glm::abs(lc);
        const int axis = (pen.x < pen.y) ? (pen.x < pen.z ? 0 : 2) : (pen.y < pen.z ? 1 : 2);
        glm::vec3 nLocal(0.0f);
        nLocal[axis] = (lc[axis] < 0.0f) ? -1.0f : 1.0f;
        c.hit         = true;
        c.normal      = Orientation * nLocal;
        c.penetration = pen[axis] + r;
        c.point       = selfPos + Orientation * cl;
        return c;
    }

    // OBB(self) vs Plane(other): projected radius along the plane normal. Normal OBB -> plane.
    Contact collidePlane(const PlaneShape& other,
                         const glm::vec3& selfPos, const glm::vec3& selfScale,
                         const glm::vec3& otherPos, const glm::vec3& /*otherScale*/) const override {
        const glm::vec3 N = glm::normalize(other.Normal);
        glm::vec3 a[3]; axes(a);
        const glm::vec3 h = HalfExtents * selfScale;
        const float R = h.x*std::abs(glm::dot(a[0],N)) + h.y*std::abs(glm::dot(a[1],N)) + h.z*std::abs(glm::dot(a[2],N));
        const float s = glm::dot(selfPos - otherPos, N);
        if (s >= R || !other.withinBounds(selfPos, otherPos)) return {};
        if (other.OneSided && s < 0.0f) return {};   // center below a one-sided plane → no contact
        Contact c;
        c.hit         = true;
        c.normal      = -N;          // OBB -> plane
        c.penetration = R - s;
        // Deepest oriented corner (support vertex in -N) for a real torque arm → tumbling.
        glm::vec3 vertex = selfPos;
        for (int i = 0; i < 3; ++i) vertex -= glm::sign(glm::dot(a[i], N)) * h[i] * a[i];
        c.point = vertex;
        return c;
    }
};

// AABB box manifold: routes plane to the corner manifold and box/OBB to the clipping manifold
// (out-of-line because the OBB branch needs the OBBShape type complete). Self is identity-axis.
inline int BoxShape::manifold(const ColliderShape& other,
                              const glm::vec3& selfPos,  const glm::vec3& selfScale,
                              const glm::vec3& otherPos, const glm::vec3& otherScale,
                              Contact* out, int maxOut) const {
    const glm::vec3 a[3] = { glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) };
    if (other.kind() == ShapeKind::Plane) {
        return boxPlaneManifold(selfPos, a, HalfExtents * selfScale,
                                static_cast<const PlaneShape&>(other), otherPos, out, maxOut);
    }
    if (other.kind() == ShapeKind::Box) {
        const glm::vec3 b[3] = { glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1) };
        const auto& o = static_cast<const BoxShape&>(other);
        return boxBoxManifold(selfPos, a, HalfExtents * selfScale,
                              otherPos, b, o.HalfExtents * otherScale, out, maxOut);
    }
    if (other.kind() == ShapeKind::OBB) {
        const auto& o = static_cast<const OBBShape&>(other);
        glm::vec3 b[3]; o.axes(b);
        return boxBoxManifold(selfPos, a, HalfExtents * selfScale,
                              otherPos, b, o.HalfExtents * otherScale, out, maxOut);
    }
    if (maxOut < 1) return 0;
    const Contact c = collide(other, selfPos, selfScale, otherPos, otherScale);
    if (!c.hit) return 0;
    out[0] = c;
    return 1;
}

// Out-of-line OBB forwarders for the other shapes (need OBBShape complete). Each forwards to
// the OBB's matching base handler and negates the normal to undo the A/B swap.
inline bool BoxShape::intersectsOBB(const OBBShape& other,
                                    const glm::vec3& selfPos, const glm::vec3& selfScale,
                                    const glm::vec3& otherPos, const glm::vec3& otherScale) const {
    return other.intersectsBox(*this, otherPos, otherScale, selfPos, selfScale);
}
inline Contact BoxShape::collideOBB(const OBBShape& other,
                                    const glm::vec3& selfPos, const glm::vec3& selfScale,
                                    const glm::vec3& otherPos, const glm::vec3& otherScale) const {
    Contact c = other.collideBox(*this, otherPos, otherScale, selfPos, selfScale);
    c.normal = -c.normal;
    return c;
}
inline bool SphereShape::intersectsOBB(const OBBShape& other,
                                       const glm::vec3& selfPos, const glm::vec3& selfScale,
                                       const glm::vec3& otherPos, const glm::vec3& otherScale) const {
    return other.intersectsSphere(*this, otherPos, otherScale, selfPos, selfScale);
}
inline Contact SphereShape::collideOBB(const OBBShape& other,
                                       const glm::vec3& selfPos, const glm::vec3& selfScale,
                                       const glm::vec3& otherPos, const glm::vec3& otherScale) const {
    Contact c = other.collideSphere(*this, otherPos, otherScale, selfPos, selfScale);
    c.normal = -c.normal;
    return c;
}
inline bool PlaneShape::intersectsOBB(const OBBShape& other,
                                      const glm::vec3& selfPos, const glm::vec3& selfScale,
                                      const glm::vec3& otherPos, const glm::vec3& otherScale) const {
    return other.intersectsPlane(*this, otherPos, otherScale, selfPos, selfScale);
}
inline Contact PlaneShape::collideOBB(const OBBShape& other,
                                      const glm::vec3& selfPos, const glm::vec3& selfScale,
                                      const glm::vec3& otherPos, const glm::vec3& otherScale) const {
    Contact c = other.collidePlane(*this, otherPos, otherScale, selfPos, selfScale);
    c.normal = -c.normal;
    return c;
}

// Oriented sphere ("ball"). Collision is IDENTICAL to a plain sphere (a sphere is
// rotation-invariant), so it reuses every SphereShape dispatch method unchanged. It only
// adds a world Orientation — refreshed from the Transform by syncShape — so a rolling ball
// can show its spin (mesh + debug spin-axis) and read its orientation for gameplay.
class OrientedSphereShape : public SphereShape {
public:
    glm::quat Orientation{1.0f, 0.0f, 0.0f, 0.0f};

    using SphereShape::SphereShape;   // inherit the radius constructor

    ShapeKind kind() const override { return ShapeKind::Ball; }
    void setOrientation(const glm::quat& q) override { Orientation = q; }
};
