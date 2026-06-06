#pragma once
#include "Core/script.h"
#include "Core/GameObject.h"
#include "Components/TransformComponent3D.h"
#include "Components/InputComponent.h"
#include "Components/CameraComponent.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

//Camera Controller. Controlls the movement of the object by WASD and rotation by mouse
class CameraController : public Script {
    TransformComponent* tf = nullptr;
    InputComponent* in = nullptr;
    CameraComponent* cam = nullptr;
public:
//TODO: add a attach variable to put any type of variable to the script
// like an array of movements to they can be used.
    float MoveSpeed = 1.0f;
    float MouseSensitivity = 0.1f;
    float PitchLimit = 89.0f;

    void start() override {
        tf = owner->getComponent<TransformComponent>();
        in = owner->getComponent<InputComponent>();
        cam = owner->getComponent<CameraComponent>();
        if (!cam) throw MissingComponentException("CameraFollow", "CameraComponent");
        //if (!tf || !in) return;   // missing dependency → silent no-op
        //TODO: modify for automatic naming on throw exception
        if (!in) throw MissingComponentException("CameraScript", "InputComponent");

        cam ->useOwnerPosition();
        cam ->useOwnerRotation();

        // Keyboard movement (WASD). Checks Enabled so disabling the script
        // also disables its callbacks (which would otherwise keep firing).
        // binds action to a object-function
        in->BindPressed("CameraMoveForward",  [this](float dt) { if (Enabled) tf->Position += tf->forward() * MoveSpeed * dt; });
        in->BindPressed("CameraMoveBackward", [this](float dt) { if (Enabled) tf->Position -= tf->forward() * MoveSpeed * dt; });
        in->BindPressed("CameraMoveLeft",     [this](float dt) { if (Enabled) tf->Position -= tf->right()   * MoveSpeed * dt; });
        in->BindPressed("CameraMoveRight",    [this](float dt) { if (Enabled) tf->Position += tf->right()   * MoveSpeed * dt; });
        in->BindPressed("CameraMoveUp",       [this](float dt) { if (Enabled) tf->Position += tf->up()   * MoveSpeed * dt; });
        in->BindPressed("CameraMoveDown",     [this](float dt) { if (Enabled) tf->Position -= tf->up()   * MoveSpeed * dt; });
        // Mouse look — yaw on x, pitch on y, clamped to avoid gimbal flip.
        in->BindMouseLook([this](float xOff, float yOff) {
            if (!Enabled) return;
            tf->RotationEuler.x += xOff * MouseSensitivity;
            tf->RotationEuler.y  = glm::clamp(tf->RotationEuler.y + yOff * MouseSensitivity,
                                              -PitchLimit, PitchLimit);
        });
    }
};
