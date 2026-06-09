#pragma once
#include "Core/Components/Component.h"
#include "Core/Components/RotationUtil.h"   // eulerToQuat (shared rotation convention)
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include "Core/Engine/Services/LoadContext.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Plain transform data: position/rotation/scale plus basis helpers. No Component
// base, so it carries no vtable pointer and no owner — embed it directly wherever
// only the data is needed (e.g. SpatialComponent::localTransform) to avoid paying
// for a full Component you never tick.
struct Transform {
    glm::vec3 Position{0.0f};
    glm::vec3 RotationEuler{0.0f};   // yaw, pitch, roll (degrees) — authoring/scripting input
    glm::vec3 Scale{1.0f};
    // Rotation authority for rendering & colliders. Derived from RotationEuler for authored /
    // Euler-animated objects; written directly (quaternion-integrated) by physics bodies. See
    // SpatialComponent3D::orientation(), which reads this or derives it from RotationEuler.
    glm::quat Orientation{1.0f, 0.0f, 0.0f, 0.0f};

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
struct TransformComponent3D : public Component, public Transform {
    // JSON: { "type": "Transform", "position": [x,y,z], "rotation": [...], "scale": [...] }
    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Position      = jsonVec3(j.value("position", nlohmann::json{}), Position);
        RotationEuler = jsonVec3(j.value("rotation", nlohmann::json{}), RotationEuler);
        Scale         = jsonVec3(j.value("scale",    nlohmann::json{}), Scale);
        Orientation   = eulerToQuat(RotationEuler);   // seed the quaternion authority
    }
};

REGISTER_COMPONENT(TransformComponent3D, "Transform")
