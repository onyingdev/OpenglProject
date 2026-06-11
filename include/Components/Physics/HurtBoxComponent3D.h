#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Components/ColliderShapes.h"     // shared shape hierarchy
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include <glm/glm.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <unordered_set>
#include <vector>
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif

// Gameplay overlap volume ("hurtbox"). The LIGHT, trigger-only counterpart to the
// physics ColliderComponent3D: it only answers "are these two overlapping?" (boolean
// intersects(), never the contact/penetration math) and fires enter/stay/exit callbacks
// from a frame-to-frame diff. No push-out, no velocity — that is the rigid body's job.
// Hurtboxes sweep their OWN registry, separate from physics colliders, so each set stays
// small and the calls stay cheap (hit detection, triggers, pickups, zones, ...).
//
// JSON: { "type": "HurtBox", "layer": 1, "mask": 4294967295,
//         "shape": { "kind": "box", "halfExtents": [x,y,z] } }   // or sphere/plane
class HurtBoxComponent3D : public SpatialComponent3D {
public:
    using Callback = std::function<void(HurtBoxComponent3D* other)>;

    uint32_t Layer = 0x1;
    uint32_t Mask  = 0xFFFFFFFF;

    std::unique_ptr<ColliderShape> Shape;

    Callback OnEnter;   // first frame this hurtbox overlaps `other`
    Callback OnStay;    // every frame the overlap persists
    Callback OnExit;    // first frame the overlap ends

    HurtBoxComponent3D() = default;
    explicit HurtBoxComponent3D(std::unique_ptr<ColliderShape> shape) : Shape(std::move(shape)) {}

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
            const bool oneSided = s.value("oneSided", true);
            glm::vec2 he{5.0f, 5.0f};
            if (s.contains("halfExtents") && s["halfExtents"].is_array() && s["halfExtents"].size() >= 2)
                he = { s["halfExtents"][0].get<float>(), s["halfExtents"][1].get<float>() };
            Shape = std::make_unique<PlaneShape>(n, infinite, he, oneSided);
        } else if (kind == "obb") {
            glm::vec3 he = s.contains("halfExtents") ? jsonVec3(s["halfExtents"], glm::vec3(0.5f))
                                                     : glm::vec3(0.5f);
            Shape = std::make_unique<OBBShape>(he);   // orientation comes from the Transform (syncShape)
        } else if (kind == "ball") {
            Shape = std::make_unique<OrientedSphereShape>(s.value("radius", 0.5f));  // oriented sphere
        } else {
            std::cerr << "[HurtBoxComponent3D] unknown shape kind: " << kind << "\n";
        }
    }

    // Orient the shape from the owner Transform (OBB orientation / plane normal / ball spin;
    // no-op for box/plain-sphere). orientation() is the quaternion authority. Called once per
    // hurtbox before each sweep.
    void syncShape() {
        if (Shape) Shape->setOrientation(orientation());
    }

    void start() override {
        SpatialComponent3D::start();
        if (!Shape) std::cerr << "Warning: HurtBoxComponent3D on " << owner->Name << " has no shape.\n";
        AllHurtBoxes().push_back(this);
    }

    ~HurtBoxComponent3D() override {
        auto& list = AllHurtBoxes();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
    }

    // Read-only access to the live-hurtbox registry (debug-draw pass).
    static const std::vector<HurtBoxComponent3D*>& hurtboxes() { return AllHurtBoxes(); }

    // Call once per frame. Naive O(N^2) overlap sweep (boolean intersects() only),
    // parallelized past 256 hurtboxes; then a frame-to-frame diff fires enter/stay/exit.
    // TODO: broadphase; only sweep compatible layers.
    static void tickWorld() {
        auto& list = AllHurtBoxes();

        for (auto* c : list) c->syncShape();   // refresh OBB orientations (no-op otherwise)

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
    std::unordered_set<HurtBoxComponent3D*> thisFrame;
    std::unordered_set<HurtBoxComponent3D*> lastFrame;

    static std::vector<HurtBoxComponent3D*>& AllHurtBoxes() {
        static std::vector<HurtBoxComponent3D*> v;
        return v;
    }

    static bool passesMask(const HurtBoxComponent3D* a, const HurtBoxComponent3D* b) {
        return layerMaskOverlap(a->Layer, a->Mask, b->Layer, b->Mask);
    }
};

REGISTER_COMPONENT(HurtBoxComponent3D, "HurtBox")
