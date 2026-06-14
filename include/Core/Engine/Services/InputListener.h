#pragma once

// Sink for raw window input events. Window owns the GLFW window user-pointer and all GLFW callbacks (so
// there's a single owner — no fighting over the one user-pointer GLFW gives per window) and forwards the
// raw events to whatever listener is registered (the InputManager). GLFW-free on purpose: Window depends
// only on this interface, not on InputManager, keeping the dependency one-way.
class IInputListener {
public:
    virtual ~IInputListener() = default;
    virtual void onCursorPos(double xpos, double ypos) = 0;
    virtual void onKey(int key, int action) = 0;   // action = GLFW_PRESS / GLFW_RELEASE / GLFW_REPEAT
};
