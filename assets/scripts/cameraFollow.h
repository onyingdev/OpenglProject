#pragma once
#include "../../include/Core/script.h"
#include "../../include/Core/GameObject.h"
#include "../../include/Components/CameraComponent.h"
#include "../../vendor/glm/glm/glm.hpp"
#include "../../vendor/glm/glm/gtc/matrix_transform.hpp"

//Camera Controller. Controlls the movement of the object by WASD and rotation by mouse
class CameraFollow : public Script {
    TransformComponent* tf = nullptr;
public:
    glm::vec3 Offset = {-3, 6, 0};
    glm::vec3 RotationOffset = {0, -60, 0};

    void start() override {
        tf = owner->getComponent<TransformComponent>();
        if (!tf) throw MissingComponentException("CameraScript", "TransformComponent");
    }

    void tick(float dt) override {
        if (!Enabled) return;
        tf->Position = tf->owner->getComponent<TransformComponent>()->Position + Offset;
        tf->RotationEuler = tf->owner->getComponent<TransformComponent>()->RotationEuler + RotationOffset;
    }
};
