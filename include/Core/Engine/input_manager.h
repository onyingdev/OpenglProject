#pragma once
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <glad/glad.h>
#include <glfw3.h>

// A single buffered key press: which action, and when (glfwGetTime seconds). `consumed` marks it as
// already taken by a ConsumeBuffered/ConsumeSequence so it won't fire again. The buffer is time-based,
// so it behaves identically at any frame rate.
struct BufferedInput {
    std::string action;
    double      time = 0.0;
    bool        consumed = false;
};

//input manager class only handles inputs and mouse. Does not handle logic.
// we can use input manager by itself but not Input Component.
class InputManager {
public:
    // Pass the window in so the manager can tie itself to it
    InputManager(GLFWwindow* window);

    //TODO: Separate input manager into logic and binding (KBM or controller binding) and event manager.
    // --- Keyboard Actions ---
    void BindAction(const std::string& actionName, int glfwKey); //this is input management
    void ClearActions(); // drop all bindings (used before re-applying a config at runtime)
    bool IsActionPressed(const std::string& actionName) const; //this is event (held this frame)
    bool IsActionReleased(const std::string& actionName) const; //fires once on the frame the key is let go

    // --- Input buffer (frame-rate-independent, time-based) ---
    // Presses are captured event-driven (GLFW key callback) and time-stamped, then kept for a tunable
    // window. Scripts read/consume the buffer to build jump-buffering, combos, etc. — held movement still
    // uses IsActionPressed. All windows are seconds; the JSON config authors them in ms.

    // Jump-buffer primitive: if there's a live (un-consumed, in-window) press of `action`, consume it and
    // return true. Call this when the action becomes *allowed* (e.g. on landing) — a press up to its
    // buffer window earlier still counts. Fires exactly once per physical press.
    bool ConsumeBuffered(const std::string& actionName);

    // Peek: is there a live buffered press of `action` (without consuming it)?
    bool IsBuffered(const std::string& actionName) const;

    // Seconds since the newest live buffered press of `action`, or -1 if none.
    double TimeSincePressed(const std::string& actionName) const;

    // Combo helper: match an ordered sequence of actions in the buffer where each consecutive press is
    // within `maxGapSeconds` of the previous. On a full match, consume the matched presses and return
    // true. (For anything more bespoke, read Buffer() directly.)
    bool ConsumeSequence(const std::vector<std::string>& sequence, double maxGapSeconds);

    // Raw read access to the chronological buffer (oldest -> newest) for custom script logic.
    const std::deque<BufferedInput>& Buffer() const { return m_buffer; }

    // Window tuning (seconds). Default applies to any action without an explicit override.
    void SetDefaultBufferTime(double seconds) { m_defaultBufferTime = seconds; }
    void SetBufferTime(const std::string& actionName, double seconds) { m_bufferTimes[actionName] = seconds; }

    // --- Mouse Handling ---
    float GetMouseOffsetX() const { return mouseOffsetX; }
    float GetMouseOffsetY() const { return mouseOffsetY; }

    // Call this at the end of every frame to stop the camera from spinning, and to prune the buffer.
    void ResetFrame();

private:
    // The GLFW callbacks must be static, but they use the UserPointer to reach the class instance.
    static void MouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    // Buffer window for an action (override else default), in seconds.
    double bufferTimeFor(const std::string& actionName) const;
    // Drop entries that can no longer be live (older than the largest window) + cap the deque size.
    void pruneBuffer(double now);

    GLFWwindow* m_Window;
    std::unordered_map<std::string, int> m_KeyBindings;
    std::unordered_map<std::string, bool> m_PreviousPressed;

    // Reverse lookup for the event-driven capture: which actions fire for a given GLFW key.
    std::unordered_map<int, std::vector<std::string>> m_KeyToActions;

    // The buffer + its tuning.
    std::deque<BufferedInput>                  m_buffer;
    std::unordered_map<std::string, double>    m_bufferTimes;          // per-action window (seconds)
    double                                     m_defaultBufferTime = 0.12;  // 120 ms
    static constexpr size_t                    kMaxBufferEntries   = 64;    // safety cap

    // Mouse State
    float lastX;
    float lastY;
    float mouseOffsetX;
    float mouseOffsetY;
    bool firstMouse;
};
