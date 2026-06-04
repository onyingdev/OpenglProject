#pragma once
#include "../Core/SpatialComponent.h"
#include "../../vendor/glm/glm/glm.hpp"
#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

// This is vibecoded needs revision ASAP
// TODO: write BoundingBoxComponent.

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
};

// -----------------------------------------------------------------------------
// ColliderComponent3D
// -----------------------------------------------------------------------------

class ColliderComponent3D : public SpatialComponent {
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

    void start() override {
        SpatialComponent::start();
        if (!Shape) Shape = std::make_unique<BoxShape>(); // sensible default
        AllColliders().push_back(this);
    }

    ~ColliderComponent3D() override {
        auto& list = AllColliders();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    // Call once per frame (e.g. from main loop). Naive O(N^2). Broadphase later.
    static void tickWorld() {
        auto& list = AllColliders();
        //std::cout << list.size() << std::endl;
        for (auto* c : list) {
            c->thisFrame.clear();
            const glm::vec3& p = c->position();
            //std::cout << "[Collider] pos=(" << p.x << ", " << p.y << ", " << p.z << ")\n";
        }

        for (size_t i = 0; i < list.size(); ++i) {
            for (size_t j = i + 1; j < list.size(); ++j) {
                auto* a = list[i];
                auto* b = list[j];
                if (!passesMask(a, b)) continue;
                if (!a->Shape || !b->Shape) continue;

                if (a->Shape->intersects(*b->Shape,
                                         a->position(), a->scale(),
                                         b->position(), b->scale())) {
                                            //std::cout << "Collision Enter " << std::endl;
                    a->thisFrame.insert(b);
                    b->thisFrame.insert(a);
                }
            }
        }

        // Diff against previous frame to fire enter/stay/exit.
        for (auto* c : list) {
            for (auto* other : c->thisFrame) {
                if (c->lastFrame.count(other)) {
                    //std::cout << "Collision Enter " << std::endl;
                    if (c->OnStay) c->OnStay(other);
                } else {
                    std::cout << "Collision Enter " << std::endl;
                    if (c->OnEnter) c->OnEnter(other);
                }
            }
            for (auto* other : c->lastFrame) {
                if (!c->thisFrame.count(other)) {
                    std::cout << "Collision Exit " << std::endl;
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
