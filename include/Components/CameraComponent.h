#pragma once
#include "Core/SpatialComponent.h"

// Pure camera: knows how to produce view/projection matrices from whatever
// position/rotation it resolves to. It does NOT decide where that data comes from.
// A script (CameraFollow, FreeCamera, OrbitCamera, ...) is responsible for calling
// useOwnerPosition()/setLocalPosition()/etc. to wire the source per axis.
class CameraComponent : public SpatialComponent {
public:
    float Fov = 45.0f;
    float NearPlane = 0.1f, FarPlane = 100.0f;

    // start() inherited from SpatialComponent: initializes the axis pointers
    // to a snapshot of the owner's transform. Scripts override per axis as needed.

    glm::mat4 viewMatrix() const {
        return glm::lookAt(position(), position() + forward(), up());
    }
    glm::mat4 projectionMatrix(float aspect) const {
        return glm::perspective(glm::radians(Fov), aspect, NearPlane, FarPlane);
    }
};
