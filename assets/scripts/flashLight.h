#pragma once
#include "Core/Components/script.h"
#include "Core/Components/GameObject.h"
#include "Core/Engine/ComponentRegistry.h"
#include "Components/Input/InputComponent.h"
#include "Components/Rendering/CameraComponent.h"
#include "Components/Rendering/ConeLightComponent.h"
#include <nlohmann/json.hpp>

class FlashLight : public Script {
    InputComponent* in = nullptr;
    InputManager* im = nullptr;
    CameraComponent* cam = nullptr; 
    ConeLightComponent* flashlight = nullptr; // the actual light data to submit to the renderer
public:
    float intensity = 1.0f; // intensity of the Light
    glm::vec3 color = glm::vec3(1.0f); // color of the Light
    bool flashlightOn = false; // state of the flashlight

    // JSON: { "type": "FlashLight", "intensity": 1.0 }
    void deserialize(const nlohmann::json& j, LoadContext&) override {
        intensity = j.value("intensity", intensity);
        color = jsonVec3(j.value("color", nlohmann::json{}), color);
    }

    void start() override {
        in = owner->getComponent<InputComponent>();
        im = in->GetManager();
        cam = owner->getComponent<CameraComponent>();
        flashlight = owner->getComponent<ConeLightComponent>();
        flashlight->Color = color;
    }

    void tick(float dt) override {
        if (!Enabled) return;
        if (im->IsActionReleased("ToggleFlashlight")) flashlightOn = !flashlightOn;
        flashlight->Enabled = flashlightOn;
    }
};

REGISTER_COMPONENT(FlashLight, "FlashLight")