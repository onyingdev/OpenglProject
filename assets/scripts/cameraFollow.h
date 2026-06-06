#pragma once
#include "Core/script.h"
#include "Core/GameObject.h"
#include "Components/CameraComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Drives a CameraComponent to follow a target GameObject with a fixed offset.
// Owns the policy choice: writes the camera owner's TransformComponent each tick,
// and tells the camera to source all three axes from owner.
class CameraFollow : public Script {
    TransformComponent* tf = nullptr;        // camera's own transform
    TransformComponent* targetTf = nullptr;  // followed object's transform
    CameraComponent*    cam = nullptr;
public:
    GameObject* Target = nullptr;
    glm::vec3 Offset = {-3, 6, 0};
    glm::vec3 RotationOffset = {0, -60, 0};

    void start() override {
        tf  = owner->getComponent<TransformComponent>();
        cam = owner->getComponent<CameraComponent>();
        if (!tf)     throw MissingComponentException("CameraFollow", "TransformComponent");
        if (!cam)    throw MissingComponentException("CameraFollow", "CameraComponent");
        if (!Target) throw MissingComponentException("CameraFollow", "Target GameObject");
        targetTf = Target->getComponent<TransformComponent>();
        if (!targetTf) throw MissingComponentException("CameraFollow", "Target TransformComponent");

        // We drive the camera by writing its owner's transform; tell the camera to read from there.
        cam->useOwnerPosition();
        cam->useOwnerRotation();
        cam->useOwnerScale();
    }

    void tick(float dt) override {
        if (!Enabled) return;
        tf->Position      = targetTf->Position      + Offset;
        tf->RotationEuler = targetTf->RotationEuler + RotationOffset;
    }
};