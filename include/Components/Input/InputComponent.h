#pragma once
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include "Core/Components/Component.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Services/LoadContext.h"
#include "Core/Engine/input_manager.h"

// Generic input component: pipes the InputManager into per-action callbacks.
// Behavior components (controllers, abilities, UI) register what to do when
// an action fires; this component itself stays agnostic of game logic.
class InputComponent : public Component {
public:
    using ActionHandler    = std::function<void(float dt)>;
    using MouseLookHandler = std::function<void(float xOffset, float yOffset)>;

    InputComponent() = default;                               // for registry construction; manager set in deserialize
    InputComponent(InputManager* manager) : m_Input(manager) {}

    // JSON: { "type": "Input" } — the InputManager is injected from LoadContext,
    // not stored in the file (it's a live runtime service).
    void deserialize(const nlohmann::json&, LoadContext& ctx) override {
        m_Input = ctx.input;
        if (!m_Input)
            std::cerr << "[InputComponent] LoadContext.input is null; input will not work\n";
    }

    // Fires every frame the action is held down.
    // These high order functions binds a function to a action string
    void BindPressed(const std::string& action, ActionHandler handler) {
        m_PressedHandlers.push_back({action, std::move(handler)});
    }

    // Fires once on the frame the action is released.
    void BindReleased(const std::string& action, ActionHandler handler) {
        m_ReleasedHandlers.push_back({action, std::move(handler)});
    }

    // Fires once per physical press, frame-rate independent, via the InputManager buffer (consumes it).
    // Use this for discrete actions (jump, attack, spawn) instead of BindPressed, which fires every frame
    // the key is held. The action's buffer window means a press slightly early still counts.
    void BindBuffered(const std::string& action, ActionHandler handler) {
        m_BufferedHandlers.push_back({action, std::move(handler)});
    }

    // Fires every frame with the mouse delta since last frame.
    void BindMouseLook(MouseLookHandler handler) {
        m_MouseLookHandler = std::move(handler);
    }

    // uses the input manager and executes the function, handler is the function (check above functions parameters)
    void update(float dt) override {
        for (const auto& [action, handler] : m_PressedHandlers) {
            if (m_Input->IsActionPressed(action))  handler(dt);
        }
        for (const auto& [action, handler] : m_ReleasedHandlers) {
            if (m_Input->IsActionReleased(action)) handler(dt);
        }
        for (const auto& [action, handler] : m_BufferedHandlers) {
            if (m_Input->ConsumeBuffered(action)) handler(dt);   // once per buffered press
        }
        //add more if necessary
        if (m_MouseLookHandler) {
            m_MouseLookHandler(m_Input->GetMouseOffsetX(), m_Input->GetMouseOffsetY());
        }
    }

    InputManager* GetManager() const { return m_Input; }

private:
    InputManager* m_Input = nullptr;
    std::vector<std::pair<std::string, ActionHandler>> m_PressedHandlers;
    std::vector<std::pair<std::string, ActionHandler>> m_ReleasedHandlers;
    std::vector<std::pair<std::string, ActionHandler>> m_BufferedHandlers;
    MouseLookHandler m_MouseLookHandler;
};

REGISTER_COMPONENT(InputComponent, "Input")
