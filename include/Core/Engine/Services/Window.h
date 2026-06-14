#pragma once
#include <glad/glad.h>   // GLADloadproc (GL backend); harmless for others
#include <glfw3.h>
#include <string>
#include <iostream>
#include "Core/Engine/Config/EngineSettings.h"
#include "Core/Engine/Services/InputListener.h"   // Window owns the GLFW callbacks, forwards to this

// GLFW window + platform wrapper. Keeps ALL GLFW out of the rest of the engine and is
// backend-aware so the same class serves OpenGL today and Vulkan/other later:
//   - OpenGL: requests a GL context, makes it current, presents via glfwSwapBuffers.
//   - Vulkan/other: GLFW_NO_API (no GL context); the renderer owns the surface,
//     swapchain and present — Window::present() is a no-op, and the renderer creates
//     a surface from handle().
//
// One window per app. glfwInit/glfwTerminate are tied to this object's lifetime, so it
// must outlive the renderer (the GL context dies with the window).
class Window {
public:
    Window(const WindowSettings& s, RenderBackend backend) : m_backend(backend) {
        if (!glfwInit()) { std::cerr << "[Window] glfwInit failed\n"; return; }

        if (backend == RenderBackend::OpenGL) {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, s.glMajor);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, s.glMinor);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        } else {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // Vulkan/other: no GL context
        }
        if (s.mode == WindowMode::Borderless) glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        // Locked windows can't be dragged-resized at all; Free/KeepAspect stay resizable.
        glfwWindowHint(GLFW_RESIZABLE, (s.resize == ResizeMode::Locked) ? GLFW_FALSE : GLFW_TRUE);

        GLFWmonitor* monitor = (s.mode == WindowMode::Fullscreen) ? glfwGetPrimaryMonitor() : nullptr;
        m_window = glfwCreateWindow(s.width, s.height, s.title.c_str(), monitor, nullptr);
        if (!m_window) { std::cerr << "[Window] failed to create GLFW window\n"; glfwTerminate(); return; }
        m_width = s.width; m_height = s.height;

        // KeepAspect: GLFW forces the window's width:height to stay at the configured ratio
        // during manual resizing, so the present pass never stretches the image (which is
        // what causes the camera to look misaligned on an odd-shaped window).
        if (s.resize == ResizeMode::KeepAspect)
            glfwSetWindowAspectRatio(m_window, s.width, s.height);

        // Window is the SOLE owner of the one user-pointer GLFW gives per window, and of every GLFW
        // callback. Raw cursor/key events are forwarded to a registered listener (the InputManager) —
        // see setInputListener. This is what prevents the user-pointer from being contended.
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
        glfwSetCursorPosCallback(m_window, cursorPosCallback);
        glfwSetKeyCallback(m_window, keyCallback);

        if (backend == RenderBackend::OpenGL) {
            glfwMakeContextCurrent(m_window);
            glfwSwapInterval(s.vsync ? 1 : 0);
        }
    }

    ~Window() {
        if (m_window) glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool        valid()  const { return m_window != nullptr; }
    GLFWwindow* handle() const { return m_window; }   // for InputManager / Vulkan surface

    // Register who receives forwarded cursor/key events (the InputManager). Null = events are dropped.
    void setInputListener(IInputListener* listener) { m_input = listener; }

    bool shouldClose() const { return glfwWindowShouldClose(m_window); }
    void requestClose()      { glfwSetWindowShouldClose(m_window, true); }
    void pollEvents()        { glfwPollEvents(); }

    // Present the finished frame. GL swaps buffers; Vulkan presents through its own
    // swapchain (the renderer's job), so this is a no-op there.
    void present() { if (m_backend == RenderBackend::OpenGL) glfwSwapBuffers(m_window); }

    // Live framebuffer size (kept current by the resize callback).
    int width()  const { return m_width; }
    int height() const { return m_height; }

    // GL function loader — pass to an OpenGL renderer's init(). Meaningless otherwise.
    static GLADloadproc glLoader() { return (GLADloadproc)glfwGetProcAddress; }

    // Monotonic seconds since init — frame timing, no graphics API involved.
    static double time() { return glfwGetTime(); }

    // FPS-style mouse capture (disabled cursor + raw motion when available).
    void captureCursor(bool capture) {
        glfwSetInputMode(m_window, GLFW_CURSOR, capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (capture && glfwRawMouseMotionSupported())
            glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

private:
    GLFWwindow*    m_window  = nullptr;
    RenderBackend  m_backend = RenderBackend::OpenGL;
    int            m_width = 0, m_height = 0;
    IInputListener* m_input = nullptr;   // forwarded cursor/key events; set via setInputListener

    static void framebufferSizeCallback(GLFWwindow* w, int width, int height) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (!self) return;
        self->m_width  = width;
        self->m_height = (height == 0) ? 1 : height;
    }

    // Forward raw cursor/key events to the listener. The user-pointer is always a Window* now, so these
    // casts are sound (this is the bug fix: InputManager no longer overwrites the pointer).
    static void cursorPosCallback(GLFWwindow* w, double xpos, double ypos) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self && self->m_input) self->m_input->onCursorPos(xpos, ypos);
    }
    static void keyCallback(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self && self->m_input) self->m_input->onKey(key, action);
    }
};
