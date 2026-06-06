#pragma once
#include "Core/Component.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Plain transform data: position/rotation/scale plus basis helpers. No Component
// base, so it carries no vtable pointer and no owner — embed it directly wherever
// only the data is needed (e.g. SpatialComponent::localTransform) to avoid paying
// for a full Component you never tick.
struct Transform {
    glm::vec3 Position{0.0f};
    glm::vec3 RotationEuler{0.0f};   // yaw, pitch, roll (degrees)
    glm::vec3 Scale{1.0f};

    glm::vec3 forward() const {
        float y = glm::radians(RotationEuler.x), p = glm::radians(RotationEuler.y);
        return glm::normalize(glm::vec3(cos(y)*cos(p), sin(p), sin(y)*cos(p)));
    }
    glm::vec3 right()   const { return glm::normalize(glm::cross(forward(), {0,1,0})); }
    glm::vec3 up()      const { return glm::normalize(glm::cross(right(), forward())); }
};

// The transform as an attachable component. Data + basis helpers are inherited
// from Transform; owner/start/update come from Component. Component is listed
// first so upcasts to Component* need no pointer adjustment (GameObject's Lookup
// stores Component* and static_casts back to TransformComponent*).
struct TransformComponent : public Component, public Transform {};
