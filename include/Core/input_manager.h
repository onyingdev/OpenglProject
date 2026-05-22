#pragma once
#include <string>
#include <unordered_map>
#include "../../vendor/glad/glad/glad.h"
#include "../../vendor/GLFW/include/glfw3.h"

//input manager class only handles inputs and mouse. Does not handle logic.
class InputManager {
public:
    // Pass the window in so the manager can tie itself to it
    InputManager(GLFWwindow* window);

    //TODO: Separate input manager into logic and binding (KBM or controller binding) and event manager.
    // --- Keyboard Actions ---
    void BindAction(const std::string& actionName, int glfwKey); //this is input management
    bool IsActionPressed(const std::string& actionName) const; //this is event
    bool IsActionReleased(const std::string& actionName) const; //fires once on the frame the key is let go

    // --- Mouse Handling ---
    float GetMouseOffsetX() const { return mouseOffsetX; }
    float GetMouseOffsetY() const { return mouseOffsetY; }

    // Call this at the end of every frame to stop the camera from spinning!
    void ResetFrame();

private:
    // The GLFW callback must be static, but it will use the UserPointer to access the class
    static void MouseCallback(GLFWwindow* window, double xpos, double ypos);

    GLFWwindow* m_Window;
    std::unordered_map<std::string, int> m_KeyBindings; 
    std::unordered_map<std::string, bool> m_PreviousPressed;

    // Mouse State
    float lastX;
    float lastY;
    float mouseOffsetX;
    float mouseOffsetY;
    bool firstMouse;
};