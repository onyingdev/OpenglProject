#pragma once
#include "../../include/Core/script.h"
#include "../../include/Core/GameObject.h"
#include "../../include/Components/TransformComponent3D.h"

// Spins the attached GameObject around the Y axis at SpinSpeed degrees/second.
class SpinModel : public Script {
    TransformComponent* tf = nullptr;
public:
    float SpinSpeed = 90.0f; // degrees per second

    void start() override {
        tf = owner->getComponent<TransformComponent>();
        if (!tf) throw MissingComponentException("SpinModel", "TransformComponent");
    }

    void tick(float dt) override {
        // RotationEuler is in degrees: x=yaw(Y), y=pitch(X), z=roll(Z). See TransformComponent3D.h.
        tf->RotationEuler.x += SpinSpeed * dt;
    }
};
