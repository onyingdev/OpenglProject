#pragma once
#include "../../include/Core/script.h"
#include "../../include/Core/GameObject.h"
#include "../../include/Components/MeshComponent.h"
//DEMO SCRIPT.

// Spins the attached GameObject around the Y axis at SpinSpeed degrees/second.
// just spins the mesh in place
class SpinModel : public Script {
    MeshComponent* mesh = nullptr;
public:
    float SpinSpeed = 180.0f; // degrees per second

    void start() override {
        mesh = owner->getComponent<MeshComponent>();
        if (!mesh) throw MissingComponentException("SpinModel", "MeshComponent");
        else mesh->RotationSource = SpatialComponent::Source::Local; //define source
    }

    void tick(float dt) override {
        // RotationEuler is in degrees: x=yaw(Y), y=pitch(X), z=roll(Z). See TransformComponent3D.h.
        mesh->localTransform.RotationEuler.x += SpinSpeed * dt;

        // Keep the angle in [0,360) to avoid float precision issues eventually causing it to stop spinning.
        // improbable but good practice. If performance is a concern, can remove this and let it spin indefinitely.
        if (mesh->localTransform.RotationEuler.x >= 360.0f) 
            mesh->localTransform.RotationEuler.x -= 360.0f;
    }
};
