#pragma once
#include "../../include/Core/script.h"
#include "../../include/Core/gameObject.h"
#include "../../include/Components/transformComponent3D.h"
#include "../../include/Components/inputComponent.h"
#include "../../vendor/glm/glm/glm.hpp"
#include "../../vendor/glm/glm/gtc/matrix_transform.hpp"

//Camera Controller. Controlls the movement of the object by WASD and rotation by mouse
class CameraController : public Script {
    TransformComponent* tf = nullptr;
    InputComponent* in = nullptr;
public:
    float MoveSpeed = 2.5f;
    float MouseSensitivity = 0.1f;
    float PitchLimit = 89.0f;

    void start() override {
        tf = owner->getComponent<TransformComponent>();
        in = owner->getComponent<InputComponent>();
        //if (!tf || !in) return;   // missing dependency → silent no-op
        //TODO: modify for automatic naming on throw exception
        if (!tf) throw MissingComponentException("CameraScript", "TransformComponent");
        if (!in) throw MissingComponentException("CameraScript", "InputComponent");

        // Keyboard movement (WASD). Checks Enabled so disabling the script
        // also disables its callbacks (which would otherwise keep firing).
        // binds action to a object-function
        in->BindPressed("MoveForward",  [this](float dt) { if (Enabled) tf->Position += tf->forward() * MoveSpeed * dt; });
        in->BindPressed("MoveBackward", [this](float dt) { if (Enabled) tf->Position -= tf->forward() * MoveSpeed * dt; });
        in->BindPressed("MoveLeft",     [this](float dt) { if (Enabled) tf->Position -= tf->right()   * MoveSpeed * dt; });
        in->BindPressed("MoveRight",    [this](float dt) { if (Enabled) tf->Position += tf->right()   * MoveSpeed * dt; });

        // Mouse look — yaw on x, pitch on y, clamped to avoid gimbal flip.
        in->BindMouseLook([this](float xOff, float yOff) {
            if (!Enabled) return;
            tf->RotationEuler.x += xOff * MouseSensitivity;
            tf->RotationEuler.y  = glm::clamp(tf->RotationEuler.y + yOff * MouseSensitivity,
                                              -PitchLimit, PitchLimit);
        });
    }
};
