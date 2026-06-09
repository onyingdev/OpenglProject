#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
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

// Euler degrees -> quaternion in the engine's yaw(Y)/pitch(X)/roll(Z) order, matching
// MeshComponent::worldMatrix so a rotated collider lines up with its mesh. Used to feed
// an OBB's world orientation from the owner Transform's rotation.
inline glm::quat eulerToQuat(const glm::vec3& deg) {
    return glm::angleAxis(glm::radians(deg.x), glm::vec3(0.0f, 1.0f, 0.0f))
         * glm::angleAxis(glm::radians(deg.y), glm::vec3(1.0f, 0.0f, 0.0f))
         * glm::angleAxis(glm::radians(deg.z), glm::vec3(0.0f, 0.0f, 1.0f));
}

// Tag for type-switching without RTTI (debug-draw, future shape-specific paths).
enum class ShapeKind { Box, Sphere, Plane, OBB };

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
};

class BoxShape : public ColliderShape {
public:
    glm::vec3 HalfExtents{0.5f}; // local half-size; world size = HalfExtents * scale

    explicit BoxShape(glm::vec3 halfExtents = glm::vec3(0.5f)) : HalfExtents(halfExtents) {}

    ShapeKind kind() const override { return ShapeKind::Box; }

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
};

//checks exclusively spheres, doesnt allow ellipsoids.
class SphereShape : public ColliderShape {
public:
    float Radius = 0.5f; // local radius; world radius = Radius * scale

    explicit SphereShape(float radius = 0.5f) : Radius(radius) {}

    ShapeKind kind() const override { return ShapeKind::Sphere; }

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

    explicit PlaneShape(glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f),
                        bool infinite = true,
                        glm::vec2 halfExtents = glm::vec2(5.0f))
        : Normal(normal), Infinite(infinite), HalfExtents(halfExtents), LocalNormal(normal) {}

    ShapeKind kind() const override { return ShapeKind::Plane; }

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
// OBB (oriented bounding box) + Separating Axis Theorem
// -----------------------------------------------------------------------------

// SAT between two boxes given world centers, unit axes, and world half-extents. Returns a
// Contact whose normal points A -> B (least-penetration axis = MTV). Tests 15 axes: A's 3
// faces, B's 3 faces, 9 edge cross-products; near-zero cross axes (parallel edges) are
// skipped. An AABB is just a box with identity axes, so this serves OBB-vs-AABB too.
inline Contact satBoxes(const glm::vec3& ca, const glm::vec3 ax[3], const glm::vec3& ha,
                        const glm::vec3& cb, const glm::vec3 bx[3], const glm::vec3& hb) {
    const glm::vec3 t = cb - ca;            // A center -> B center
    float     minPen = std::numeric_limits<float>::max();
    glm::vec3 bestAxis(0.0f);
    bool      separated = false;

    auto testAxis = [&](glm::vec3 L) {
        if (separated) return;
        const float len = glm::length(L);
        if (len < 1e-6f) return;            // degenerate (parallel edges) — covered by face axes
        L /= len;
        const float ra = ha.x*std::abs(glm::dot(ax[0],L)) + ha.y*std::abs(glm::dot(ax[1],L)) + ha.z*std::abs(glm::dot(ax[2],L));
        const float rb = hb.x*std::abs(glm::dot(bx[0],L)) + hb.y*std::abs(glm::dot(bx[1],L)) + hb.z*std::abs(glm::dot(bx[2],L));
        const float pen = (ra + rb) - std::abs(glm::dot(t, L));
        if (pen < 0.0f) { separated = true; return; }
        if (pen < minPen) { minPen = pen; bestAxis = (glm::dot(t, L) < 0.0f) ? -L : L; }
    };

    for (int i = 0; i < 3; ++i) testAxis(ax[i]);
    for (int i = 0; i < 3; ++i) testAxis(bx[i]);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) testAxis(glm::cross(ax[i], bx[j]));
    if (separated) return {};

    Contact c;
    c.hit         = true;
    c.normal      = bestAxis;               // A -> B
    c.penetration = minPen;
    c.point       = ca + 0.5f * t;          // approximate (linear solver ignores point)
    return c;
}

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

    // World axes (unit columns of the rotation matrix).
    void axes(glm::vec3 out[3]) const {
        const glm::mat3 R = glm::mat3_cast(Orientation);
        out[0] = R[0]; out[1] = R[1]; out[2] = R[2];
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
        Contact c;
        c.hit         = true;
        c.normal      = -N;          // OBB -> plane
        c.penetration = R - s;
        c.point       = selfPos - N * R;
        return c;
    }
};

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
