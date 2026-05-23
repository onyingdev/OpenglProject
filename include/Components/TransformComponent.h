#pragma once
#include "../Core/component.h"
#include "../../vendor/glm/glm/glm.hpp"
#include "../../vendor/glm/glm/gtc/matrix_transform.hpp"

struct TransformComponent : public Component {
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
