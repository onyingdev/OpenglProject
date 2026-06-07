#pragma once
#include "Core/Components/Component.h"
#include "Core/Components/GameObject.h"
#include "Core/Components/TransformComponent3D.h"
#include "Core/Engine/ComponentRegistry.h"
#include "Core/Engine/JsonHelpers.h"
#include "Components/Rendering/PointLightComponent.h"   // kDefaultLightColor
#include "Render/API/RenderTypes.h"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

// A directional light: the "sun". It has a direction but no position (parallel rays),
// so it isn't tied to the owner's location. Direction defaults to a typical overhead
// sun angle, or follows the owner's forward() if "useOwnerForward" is set. Color
// defaults to kDefaultLightColor; ambient/diffuse/specular are intensity fractions of
// that color (defaults reproduce the old hardcoded sun). Usually a scene has one.
//
// JSON: { "type": "DirectionalLight", "direction": [x,y,z], "useOwnerForward": false,
//         "color": [r,g,b], "ambient": 0.05, "diffuse": 0.30, "specular": 0.40,
//         "enabled": true }
class DirectionalLightComponent : public Component {
public:
    glm::vec3 Direction{-0.2f, -1.0f, -0.3f};   // default overhead sun
    bool      UseOwnerForward = false;          // true -> aim along owner forward()
    glm::vec3 Color = kDefaultLightColor;
    float     Ambient  = 0.05f;                 // intensity fractions of Color
    float     Diffuse  = 0.30f;
    float     Specular = 0.40f;
    bool      Enabled  = true;

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Direction       = jsonVec3(j.value("direction", nlohmann::json{}), Direction);
        UseOwnerForward = j.value("useOwnerForward", UseOwnerForward);
        Color           = jsonVec3(j.value("color", nlohmann::json{}), Color);
        Ambient         = j.value("ambient",  Ambient);
        Diffuse         = j.value("diffuse",  Diffuse);
        Specular        = j.value("specular", Specular);
        Enabled         = j.value("enabled",  Enabled);
    }

    // Build a renderer DirLight. Disabled -> black (no contribution).
    DirLight toDirLight() const {
        DirLight dl;
        dl.direction = Direction;
        if (UseOwnerForward) {
            if (auto* tf = owner->getComponent<TransformComponent3D>()) dl.direction = tf->forward();
        }
        const glm::vec3 c = Enabled ? Color : glm::vec3(0.0f);
        dl.ambient  = Ambient  * c;
        dl.diffuse  = Diffuse  * c;
        dl.specular = Specular * c;
        return dl;
    }
};

REGISTER_COMPONENT(DirectionalLightComponent, "DirectionalLight")
