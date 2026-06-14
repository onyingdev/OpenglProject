#include "Core/Engine/input_manager.h"

#include <algorithm>

InputManager::InputManager(GLFWwindow* window)
    : m_Window(window), firstMouse(true), mouseOffsetX(0.0f), mouseOffsetY(0.0f)
{
    glfwSetWindowUserPointer(m_Window, this);

    // Tell GLFW to use our static class methods for the mouse + keyboard.
    glfwSetCursorPosCallback(m_Window, MouseCallback);
    // Event-driven key capture feeds the input buffer (frame-rate independent — every transition GLFW
    // queues is delivered, even several within one poll).
    glfwSetKeyCallback(m_Window, KeyCallback);
}

void InputManager::BindAction(const std::string& actionName, int glfwKey) {
    m_KeyBindings[actionName] = glfwKey;
    m_KeyToActions[glfwKey].push_back(actionName);   // reverse map for the event-driven buffer
}

void InputManager::ClearActions() {
    m_KeyBindings.clear();
    m_PreviousPressed.clear();
    m_KeyToActions.clear();
    m_bufferTimes.clear();
    m_buffer.clear();
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

// --- input buffer ---------------------------------------------------------------------------------

double InputManager::bufferTimeFor(const std::string& actionName) const {
    auto it = m_bufferTimes.find(actionName);
    return it != m_bufferTimes.end() ? it->second : m_defaultBufferTime;
}

bool InputManager::ConsumeBuffered(const std::string& actionName) {
    const double now = glfwGetTime();
    const double window = bufferTimeFor(actionName);
    // Newest first: most recent intent wins.
    for (auto it = m_buffer.rbegin(); it != m_buffer.rend(); ++it) {
        if (it->consumed || it->action != actionName) continue;
        if (now - it->time > window) break;   // older entries of this action are even staler
        it->consumed = true;
        return true;
    }
    return false;
}

bool InputManager::IsBuffered(const std::string& actionName) const {
    const double now = glfwGetTime();
    const double window = bufferTimeFor(actionName);
    for (auto it = m_buffer.rbegin(); it != m_buffer.rend(); ++it) {
        if (it->consumed || it->action != actionName) continue;
        if (now - it->time > window) break;
        return true;
    }
    return false;
}

double InputManager::TimeSincePressed(const std::string& actionName) const {
    const double now = glfwGetTime();
    const double window = bufferTimeFor(actionName);
    for (auto it = m_buffer.rbegin(); it != m_buffer.rend(); ++it) {
        if (it->consumed || it->action != actionName) continue;
        if (now - it->time > window) break;
        return now - it->time;
    }
    return -1.0;
}

bool InputManager::ConsumeSequence(const std::vector<std::string>& sequence, double maxGapSeconds) {
    if (sequence.empty()) return false;
    const double now = glfwGetTime();

    // Greedy ordered match over the live buffer (oldest -> newest). Track the matched entries so we can
    // consume them all on a full match. Each step must be within maxGapSeconds of the previous one, and
    // within its own action's window (so a stale lone press doesn't anchor a combo).
    std::vector<std::deque<BufferedInput>::iterator> matched;
    size_t step = 0;
    double prevTime = 0.0;
    for (auto it = m_buffer.begin(); it != m_buffer.end() && step < sequence.size(); ++it) {
        if (it->consumed) continue;
        if (it->action != sequence[step]) continue;
        if (now - it->time > bufferTimeFor(it->action)) continue;     // too old to count
        if (step > 0 && it->time - prevTime > maxGapSeconds) {
            // Gap too large: restart, but this entry may still open a fresh sequence at step 0.
            matched.clear();
            step = 0;
            if (it->action != sequence[0]) continue;
        }
        matched.push_back(it);
        prevTime = it->time;
        ++step;
    }

    if (step < sequence.size()) return false;
    for (auto& it : matched) it->consumed = true;
    return true;
}

void InputManager::pruneBuffer(double now) {
    // Drop entries older than the largest configured window (they can never be live), and cap size.
    double maxWindow = m_defaultBufferTime;
    for (const auto& kv : m_bufferTimes) maxWindow = std::max(maxWindow, kv.second);

    while (!m_buffer.empty() && now - m_buffer.front().time > maxWindow)
        m_buffer.pop_front();
    while (m_buffer.size() > kMaxBufferEntries)
        m_buffer.pop_front();
}

void InputManager::ResetFrame() {
    // If we don't reset these, the camera will keep spinning even when the mouse stops
    mouseOffsetX = 0.0f;
    mouseOffsetY = 0.0f;

    // Snapshot every action's state so the next frame can detect press/release edges
    for (const auto& binding : m_KeyBindings) {
        m_PreviousPressed[binding.first] = glfwGetKey(m_Window, binding.second) == GLFW_PRESS;
    }

    pruneBuffer(glfwGetTime());   // bound the buffer's memory; liveness is still checked per query
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

// Event-driven key capture: on each rising edge (GLFW_PRESS), push a time-stamped entry for every action
// bound to that key. REPEAT (OS auto-repeat) and RELEASE are ignored — the buffer is about discrete
// presses. Runs during glfwPollEvents on the main thread.
void InputManager::KeyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    InputManager* manager = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!manager) return;

    auto it = manager->m_KeyToActions.find(key);
    if (it == manager->m_KeyToActions.end()) return;

    const double now = glfwGetTime();
    for (const std::string& act : it->second)
        manager->m_buffer.push_back(BufferedInput{ act, now, false });
}
