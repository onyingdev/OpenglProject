#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Core/Component.h"
#include "Core/input_manager.h"

// Generic input component: pipes the InputManager into per-action callbacks.
// Behavior components (controllers, abilities, UI) register what to do when
// an action fires; this component itself stays agnostic of game logic.
class InputComponent : public Component {
public:
    using ActionHandler    = std::function<void(float dt)>;
    using MouseLookHandler = std::function<void(float xOffset, float yOffset)>;

    InputComponent(InputManager* manager) : m_Input(manager) {}

    // Fires every frame the action is held down.
    // These high order functions binds a function to a action string
    void BindPressed(const std::string& action, ActionHandler handler) {
        m_PressedHandlers.push_back({action, std::move(handler)});
    }

    // Fires once on the frame the action is released.
    void BindReleased(const std::string& action, ActionHandler handler) {
        m_ReleasedHandlers.push_back({action, std::move(handler)});
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
    MouseLookHandler m_MouseLookHandler;
};
