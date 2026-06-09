#pragma once
#include "Core/Components/script.h"
#include "Core/Components/GameObject.h"
#include "Core/Components/TransformComponent3D.h"
#include "Components/Input/InputComponent.h"
#include "Components/Rendering/CameraComponent.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

//Camera Controller. Controlls the movement of the object by WASD and rotation by mouse.
// JSON: { "type": "CameraController", "moveSpeed": 1.0,
//         "mouseSensitivity": 0.1, "pitchLimit": 89.0 }
class CameraController : public Script {
    TransformComponent3D* tf = nullptr;
    InputComponent* in = nullptr;
    CameraComponent* cam = nullptr;
public:
//TODO: add a attach variable to put any type of variable to the script
// like an array of movements to they can be used.
    float MoveSpeed = 1.0f;
    float MouseSensitivity = 0.1f;
    float PitchLimit = 89.0f;

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        MoveSpeed        = j.value("moveSpeed",        MoveSpeed);
        MouseSensitivity = j.value("mouseSensitivity", MouseSensitivity);
        PitchLimit       = j.value("pitchLimit",       PitchLimit);
    }

    void start() override {
        tf = owner->getComponent<TransformComponent3D>();
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
        in->BindPressed("MoveForward",  [this](float dt) { if (Enabled) tf->Position += tf->forward() * MoveSpeed * dt; });
        in->BindPressed("MoveBackward", [this](float dt) { if (Enabled) tf->Position -= tf->forward() * MoveSpeed * dt; });
        in->BindPressed("MoveLeft",     [this](float dt) { if (Enabled) tf->Position -= tf->right()   * MoveSpeed * dt; });
        in->BindPressed("MoveRight",    [this](float dt) { if (Enabled) tf->Position += tf->right()   * MoveSpeed * dt; });
        in->BindPressed("MoveUp",       [this](float dt) { if (Enabled) tf->Position += tf->up()   * MoveSpeed * dt; });
        in->BindPressed("MoveDown",     [this](float dt) { if (Enabled) tf->Position -= tf->up()   * MoveSpeed * dt; });
        // Mouse look — yaw on x, pitch on y, clamped to avoid gimbal flip.
        in->BindMouseLook([this](float xOff, float yOff) {
            if (!Enabled) return;
            tf->RotationEuler.x += xOff * MouseSensitivity;
            tf->RotationEuler.y  = glm::clamp(tf->RotationEuler.y + yOff * MouseSensitivity,
                                              -PitchLimit, PitchLimit);
        });
    }
};

REGISTER_COMPONENT(CameraController, "CameraController")
