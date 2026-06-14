#include "Core/Engine/input_manager.h"

#include <algorithm>

InputManager::InputManager(GLFWwindow* window)
    : m_Window(window), firstMouse(true), mouseOffsetX(0.0f), mouseOffsetY(0.0f)
{
    // Window owns the GLFW window user-pointer and all GLFW callbacks; it forwards cursor/key events to
    // us via IInputListener (see Window::setInputListener). We only keep the handle for glfwGetKey polling.
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

// Forwarded by Window from glfwSetCursorPosCallback (main thread, during glfwPollEvents).
void InputManager::onCursorPos(double xposIn, double yposIn) {
    // Explicitly cast to float immediately to prevent truncation stepping
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    // Accumulate: glfwPollEvents() fires this once per queued cursor event, often many times per frame.
    // Using '=' would keep only the last sub-step; '+=' sums them into the full frame delta.
    mouseOffsetX += xpos - lastX;
    mouseOffsetY += lastY - ypos; // Reversed since Y goes bottom to top

    lastX = xpos;
    lastY = ypos;
}

// Forwarded by Window from glfwSetKeyCallback. On each rising edge (GLFW_PRESS), push a time-stamped
// entry for every action bound to that key. REPEAT (OS auto-repeat) and RELEASE are ignored — the buffer
// is about discrete presses. Runs during glfwPollEvents on the main thread.
void InputManager::onKey(int key, int action) {
    if (action != GLFW_PRESS) return;

    auto it = m_KeyToActions.find(key);
    if (it == m_KeyToActions.end()) return;

    const double now = glfwGetTime();
    for (const std::string& act : it->second)
        m_buffer.push_back(BufferedInput{ act, now, false });
}
