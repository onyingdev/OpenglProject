#pragma once
#include "../Core/component.h"
#include "TransformComponent.h"
class CameraComponent : public Component {
    TransformComponent* tf = nullptr;
public:
    float Fov = 45.0f;
    float NearPlane = 0.1f, FarPlane = 100.0f;

    void start() override {
        tf = owner->getComponent<TransformComponent>();   // cache pointer
    }
    glm::mat4 viewMatrix() const {
        return glm::lookAt(tf->Position, tf->Position + tf->forward(), tf->up());
    }
    glm::mat4 projectionMatrix(float aspect) const {
        return glm::perspective(glm::radians(Fov), aspect, NearPlane, FarPlane);
    }
};
