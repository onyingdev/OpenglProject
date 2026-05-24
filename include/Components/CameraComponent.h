#pragma once
#include "../Core/SpatialComponent.h"

class CameraComponent : public SpatialComponent {
    TransformComponent* tf = nullptr;
public:

    float Fov = 45.0f;
    float NearPlane = 0.1f, FarPlane = 100.0f;

    void start() override {
        tf = owner->getComponent<TransformComponent>();   // cache pointer
        if (!tf) throw MissingComponentException("CameraComponent", "TransformComponent");
    }
    glm::mat4 viewMatrix() const {
        return glm::lookAt(tf->Position, tf->Position + tf->forward(), tf->up());
    }
    glm::mat4 projectionMatrix(float aspect) const {
        return glm::perspective(glm::radians(Fov), aspect, NearPlane, FarPlane);
    }
};
