#pragma once
#include "Core/Components/Script.h"
#include "Core/Components/GameObject.h"
#include "Core/Engine/ComponentRegistry.h"
#include "Components/Rendering/MeshComponent.h"
#include <nlohmann/json.hpp>
//DEMO SCRIPT.

// Spins the attached GameObject around the Y axis at SpinSpeed degrees/second.
// just spins the mesh in place
class SpinModel : public Script {
    MeshComponent* mesh = nullptr;
public:
    float SpinSpeed = 180.0f; // degrees per second

    // JSON: { "type": "SpinModel", "spinSpeed": 180.0 }
    void deserialize(const nlohmann::json& j, LoadContext&) override {
        SpinSpeed = j.value("spinSpeed", SpinSpeed);
    }

    void start() override {
        mesh = owner->getComponent<MeshComponent>();
        if (!mesh) throw MissingComponentException("SpinModel", "MeshComponent");
    }

    void tick(float dt) override {
        // RotationEuler is in degrees: x=yaw(Y), y=pitch(X), z=roll(Z). See TransformComponent3D.h.
        // localRotation() returns a mutable ref and switches the rotation axis to local in the same call.
        glm::vec3& rot = mesh->localRotation();
        rot.x += SpinSpeed * dt;

        // Keep the angle in [0,360) to avoid float precision issues eventually causing it to stop spinning.
        // improbable but good practice. If performance is a concern, can remove this and let it spin indefinitely.
        if (rot.x >= 360.0f) rot.x -= 360.0f;
    }
};

REGISTER_COMPONENT(SpinModel, "SpinModel")
