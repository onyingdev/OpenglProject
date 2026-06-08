#pragma once
#include "Core/Components/script.h"
#include "Core/Components/GameObject.h"
#include "Components/Rendering/CameraComponent.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include "Core/Engine/SceneManager.h"      // resolve the target by name through the root
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <string>

// Drives a CameraComponent to follow a target GameObject with a fixed offset.
// The target is referenced by NAME and resolved through the SceneManager, so the
// camera can be a loose/root object (owner->scene() is null) and still find a target
// living in some scene. Re-resolving each tick means it survives scene hot-reloads
// (the old target is freed, the new one is found by name).
//
// JSON: { "type": "CameraFollow", "target": "Player",
//         "offset": [-3,6,0], "rotationOffset": [0,-60,0] }
class CameraFollow : public Script {
    TransformComponent3D* tf = nullptr;        // camera's own transform
    TransformComponent3D* targetTf = nullptr;  // followed object's transform
    CameraComponent*    cam = nullptr;
public:
    std::string TargetName = "Player";   // resolved through SceneManager
    GameObject* Target = nullptr;
    glm::vec3 Offset = {-3, 6, 0};
    glm::vec3 RotationOffset = {0, -60, 0};

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        TargetName     = j.value("target", TargetName);
        Offset         = jsonVec3(j.value("offset", nlohmann::json{}), Offset);
        RotationOffset = jsonVec3(j.value("rotationOffset", nlohmann::json{}), RotationOffset);
    }

    void start() override {
        tf  = owner->getComponent<TransformComponent3D>();
        cam = owner->getComponent<CameraComponent>();
        if (!tf)  throw MissingComponentException("CameraFollow", "TransformComponent3D");
        if (!cam) throw MissingComponentException("CameraFollow", "CameraComponent");

        // We drive the camera by writing its owner's transform; tell the camera to read from there.
        cam->useOwnerPosition();
        cam->useOwnerRotation();
        cam->useOwnerScale();

        resolveTarget();   // best-effort; tick() retries if the target isn't up yet
    }

    void tick(float dt) override {
        if (!Enabled) return;
        // Resolve by name EVERY tick: cheap (a hash lookup), and the only safe option
        // across a scene hot-reload — the old target is freed, so a cached pointer would
        // dangle (and stay non-null). Re-resolving picks up the rebuilt target or null.
        resolveTarget();
        if (!targetTf) return;            // nothing to follow yet
        tf->Position      = targetTf->Position      + Offset;
        tf->RotationEuler = targetTf->RotationEuler + RotationOffset;
    }

private:
    void resolveTarget() {
        Target = TargetName.empty() ? nullptr
                                    : SceneManager::instance().findObjectInScenes(TargetName);
        targetTf = Target ? Target->getComponent<TransformComponent3D>() : nullptr;
    }
};

REGISTER_COMPONENT(CameraFollow, "CameraFollow")
