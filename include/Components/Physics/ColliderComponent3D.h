#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Components/ColliderShapes.h"     // shared shape hierarchy + Contact
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>

// Physics collider: a body's collision SURFACE. Owns a shape and a layer/mask, and
// registers into a static list the rigid-body solver sweeps for contacts
// (OldRigidBodyComponent3D::stepWorld -> colliders() + Shape->collide()). This is the
// PHYSICS half only; gameplay overlap triggers (enter/stay/exit) live in the separate,
// lighter HurtBoxComponent3D. The two sweep independent registries on purpose.
//
// JSON: { "type": "Collider", "layer": 1, "mask": 4294967295,
//         "shape": { "kind": "box", "halfExtents": [x,y,z] } }   // or sphere/plane
class ColliderComponent3D : public SpatialComponent3D {
public:
    // Filtering: a pair interacts only if (a.Layer & b.Mask) && (b.Layer & a.Mask).
    uint32_t Layer = 0x1;
    uint32_t Mask  = 0xFFFFFFFF;

    std::unique_ptr<ColliderShape> Shape;

    ColliderComponent3D() = default;
    explicit ColliderComponent3D(std::unique_ptr<ColliderShape> shape) : Shape(std::move(shape)) {}

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
        } else if (kind == "obb") {
            glm::vec3 he = s.contains("halfExtents") ? jsonVec3(s["halfExtents"], glm::vec3(0.5f))
                                                     : glm::vec3(0.5f);
            Shape = std::make_unique<OBBShape>(he);   // orientation comes from the Transform (syncShape)
        } else if (kind == "ball") {
            Shape = std::make_unique<OrientedSphereShape>(s.value("radius", 0.5f));  // oriented sphere
        } else {
            std::cerr << "[ColliderComponent3D] unknown shape kind: " << kind << "\n";
        }
    }

    // Orient the shape from the owner Transform (OBB orientation / plane normal / ball spin;
    // no-op for box/plain-sphere). orientation() is the quaternion authority: derived from
    // Euler for authored objects, the integrated quat for physics bodies. Called once per
    // collider before each narrowphase sweep.
    void syncShape() {
        if (Shape) Shape->setOrientation(orientation());
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
    // (OldRigidBodyComponent3D::stepWorld) and the debug-draw pass.
    static const std::vector<ColliderComponent3D*>& colliders() { return AllColliders(); }

    // Public layer/mask filter so the solver reuses the same rule.
    static bool masksOverlap(const ColliderComponent3D* a, const ColliderComponent3D* b) {
        return layerMaskOverlap(a->Layer, a->Mask, b->Layer, b->Mask);
    }

private:
    static std::vector<ColliderComponent3D*>& AllColliders() {
        static std::vector<ColliderComponent3D*> v;
        return v;
    }
};

REGISTER_COMPONENT(ColliderComponent3D, "Collider")
