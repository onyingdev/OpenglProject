#pragma once
#include "Core/Components/Component.h"
#include "Core/Components/GameObject.h"
#include "Core/Components/TransformComponent3D.h"
#include "Core/Engine/ComponentRegistry.h"
#include "Core/Engine/JsonHelpers.h"
#include "Render/API/RenderTypes.h"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

// The color a light uses when the data doesn't assign one. Override per-light with
// a "color" entry in JSON. Shared by all light components.
inline const glm::vec3 kDefaultLightColor{1.0f, 1.0f, 1.0f};

// A point light attached to a GameObject. Its position comes from the owner's
// TransformComponent3D, so moving the object moves the light. Color defaults to
// kDefaultLightColor and is overridden by "color" in the component's JSON.
//
// JSON: { "type": "PointLight", "color": [r,g,b],
//         "constant": 1.0, "linear": 0.09, "quadratic": 0.032,
//         "attenuation": 1.0, "ambient": 0.05, "enabled": true }
class PointLightComponent : public Component {
public:
    glm::vec3 Color = kDefaultLightColor;   // default unless assigned
    float Constant  = 1.0f;
    float Linear    = 0.09f;
    float Quadratic = 0.032f;
    float Ambient   = 0.05f;   // ambient contribution as a fraction of Color
    float Attenuation = 1.0f; // multiplier for attenuation; can be used to make the light reach farther than the default
    bool  Enabled   = true;

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Color     = jsonVec3(j.value("color", nlohmann::json{}), Color);  // keep default if absent
        Constant  = j.value("constant",  Constant);
        Linear    = j.value("linear",    Linear);
        Quadratic = j.value("quadratic", Quadratic);
        Ambient   = j.value("ambient",   Ambient);
        Attenuation = j.value("attenuation", Attenuation);
        Enabled   = j.value("enabled",   Enabled);
    }

    // Build a renderer PointLight at the owner's current position.
    PointLight toPointLight() const {
        PointLight pl;
        if (auto* tf = owner->getComponent<TransformComponent3D>()) pl.position = tf->Position;
        pl.constant  = Constant;
        pl.linear    = Linear;
        pl.quadratic = Quadratic;
        pl.ambient   = Ambient * Color;
        pl.diffuse   = Color;
        pl.specular  = Color;
        pl.attenuation = Attenuation;
        pl.enabled   = Enabled;
        return pl;
    }
};

REGISTER_COMPONENT(PointLightComponent, "PointLight")
