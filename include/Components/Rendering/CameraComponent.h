#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Engine/ComponentRegistry.h"
#include <nlohmann/json.hpp>

// Pure camera: knows how to produce view/projection matrices from whatever
// position/rotation it resolves to. It does NOT decide where that data comes from.
// A script (CameraFollow, FreeCamera, OrbitCamera, ...) is responsible for calling
// useOwnerPosition()/setLocalPosition()/etc. to wire the source per axis.
class CameraComponent : public SpatialComponent3D {
public:
    float Fov = 45.0f;
    float NearPlane = 0.1f, FarPlane = 100.0f;

    // start() inherited from SpatialComponent3D: initializes the axis pointers
    // to a snapshot of the owner's transform. Scripts override per axis as needed.

    glm::mat4 viewMatrix() const {
        return glm::lookAt(position(), position() + forward(), up());
    }
    glm::mat4 projectionMatrix(float aspect) const {
        return glm::perspective(glm::radians(Fov), aspect, NearPlane, FarPlane);
    }

    // JSON: { "type": "Camera", "fov": 45, "near": 0.1, "far": 100 }
    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Fov       = j.value("fov",  Fov);
        NearPlane = j.value("near", NearPlane);
        FarPlane  = j.value("far",  FarPlane);
    }
};

REGISTER_COMPONENT(CameraComponent, "Camera")
