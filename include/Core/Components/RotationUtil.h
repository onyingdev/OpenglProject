#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Shared rotation helper. The engine authors rotation as Euler degrees in yaw(Y)/pitch(X)/
// roll(Z) order (see Transform::RotationEuler and MeshComponent::worldMatrix). This converts
// that to a quaternion so the same convention is used everywhere a quaternion is needed
// (Transform::Orientation, OBB orientation, the physics integrator). One definition, shared
// by the transform/component layer and the collision-shape layer.
inline glm::quat eulerToQuat(const glm::vec3& deg) {
    return glm::angleAxis(glm::radians(deg.x), glm::vec3(0.0f, 1.0f, 0.0f))
         * glm::angleAxis(glm::radians(deg.y), glm::vec3(1.0f, 0.0f, 0.0f))
         * glm::angleAxis(glm::radians(deg.z), glm::vec3(0.0f, 0.0f, 1.0f));
}
