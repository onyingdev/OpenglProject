#include "Core/Engine/input_manager.h"

InputManager::InputManager(GLFWwindow* window)
    : m_Window(window), firstMouse(true), mouseOffsetX(0.0f), mouseOffsetY(0.0f)
{
    glfwSetWindowUserPointer(m_Window, this);

    // Tell GLFW to use our static class method for the mouse
    glfwSetCursorPosCallback(m_Window, MouseCallback);
}

void InputManager::BindAction(const std::string& actionName, int glfwKey) {
    m_KeyBindings[actionName] = glfwKey;
}

void InputManager::ClearActions() {
    m_KeyBindings.clear();
    m_PreviousPressed.clear();
}

bool InputManager::IsActionPressed(const std::string& actionName) const {
    auto it = m_KeyBindings.find(actionName); //TODO: optimize search by using an ordered map or hash map
    if (it != m_KeyBindings.end()) {
        return glfwGetKey(m_Window, it->second) == GLFW_PRESS;
    }
    return false; // Action string not found
}

// Fires once on the held -> released edge: down last frame, up this frame.
bool InputManager::IsActionReleased(const std::string& actionName) const {
    auto it = m_KeyBindings.find(actionName);
    if (it == m_KeyBindings.end())
        return false; // Action string not found

    bool isDownNow = glfwGetKey(m_Window, it->second) == GLFW_PRESS;

    auto prevIt = m_PreviousPressed.find(actionName);
    bool wasDownLastFrame = (prevIt != m_PreviousPressed.end()) && prevIt->second;

    return wasDownLastFrame && !isDownNow;
}

void InputManager::ResetFrame() {
    // If we don't reset these, the camera will keep spinning even when the mouse stops
    mouseOffsetX = 0.0f;
    mouseOffsetY = 0.0f;

    // Snapshot every action's state so the next frame can detect press/release edges
    for (const auto& binding : m_KeyBindings) {
        m_PreviousPressed[binding.first] = glfwGetKey(m_Window, binding.second) == GLFW_PRESS;
    }
}

void InputManager::MouseCallback(GLFWwindow* window, double xposIn, double yposIn) {
    // Retrieve the C++ instance we tied to the window earlier
    InputManager* manager = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!manager) return;

    // FIX 2: Explicitly cast to float immediately to prevent truncation stepping
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (manager->firstMouse) {
        manager->lastX = xpos;
        manager->lastY = ypos;
        manager->firstMouse = false;
    }

    // Accumulate: glfwPollEvents() fires this callback once per queued cursor
    // event, often many times per frame. Using '=' would keep only the last
    // sub-step and discard the rest; '+=' sums them into the full frame delta.
    manager->mouseOffsetX += xpos - manager->lastX;
    manager->mouseOffsetY += manager->lastY - ypos; // Reversed since Y goes bottom to top

    manager->lastX = xpos;
    manager->lastY = ypos;
}