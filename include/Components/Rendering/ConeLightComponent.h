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

// A cone (spot) light attached to a GameObject. Position comes from the owner's
// TransformComponent3D; direction defaults to the owner's forward() unless the JSON
// gives an explicit "direction". InnerAngle/OuterAngle are the cone half-angles in
// DEGREES — the soft edge falls off between them. Color defaults to kDefaultLightColor
// and is overridden by "color".
//
// JSON: { "type": "ConeLight", "color": [r,g,b],
//         "direction": [x,y,z],            // optional; omit to track owner forward
//         "innerAngle": 12.5, "outerAngle": 17.5,
//         "constant": 1.0, "linear": 0.09, "quadratic": 0.032,
//         "enabled": true }
class ConeLightComponent : public Component {
public:
    glm::vec3 Color = kDefaultLightColor;   // default unless assigned
    glm::vec3 Direction{0.0f, 0.0f, -1.0f}; // used only when HasDirection
    bool      HasDirection = false;         // false -> follow owner forward()
    float     InnerAngle = 12.5f;           // degrees: full-bright cone
    float     OuterAngle = 17.5f;           // degrees: outer edge (fade to here)
    float     Constant  = 1.0f;
    float     Linear    = 0.09f;
    float     Quadratic = 0.032f;
    bool      Enabled   = true;

    void deserialize(const nlohmann::json& j, LoadContext&) override {
        Color = jsonVec3(j.value("color", nlohmann::json{}), Color);   // keep default if absent
        if (j.contains("direction")) {
            Direction = jsonVec3(j["direction"], Direction);
            HasDirection = true;
        }
        InnerAngle = j.value("innerAngle", InnerAngle);
        OuterAngle = j.value("outerAngle", OuterAngle);
        Constant   = j.value("constant",  Constant);
        Linear     = j.value("linear",    Linear);
        Quadratic  = j.value("quadratic", Quadratic);
        Enabled    = j.value("enabled",   Enabled);

        // Guarantee a non-zero soft edge so the shader's (cutOff - outerCutOff) is
        // never zero (which would divide-by-zero into a NaN).
        if (OuterAngle <= InnerAngle) OuterAngle = InnerAngle + 1.0f;
    }

    // Build a renderer SpotLight from the owner's current pose.
    SpotLight toSpotLight() const {
        SpotLight s;
        if (auto* tf = owner->getComponent<TransformComponent3D>()) {
            s.position  = tf->Position;
            s.direction = HasDirection ? Direction : tf->forward();
        } else {
            s.direction = Direction;
        }
        // cutOff/outerCutOff are stored as cosines (what the shader compares against).
        // Larger angle => smaller cosine, so outerCutOff < cutOff.
        s.cutOff      = glm::cos(glm::radians(InnerAngle));
        s.outerCutOff = glm::cos(glm::radians(OuterAngle));
        s.constant    = Constant;
        s.linear      = Linear;
        s.quadratic   = Quadratic;
        s.ambient     = glm::vec3(0.0f);
        s.diffuse     = Color;
        s.specular    = Color;
        s.enabled     = Enabled;
        return s;
    }
};

REGISTER_COMPONENT(ConeLightComponent, "ConeLight")
