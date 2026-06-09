#pragma once
#include <memory>
#include <string>
#include <iostream>

#include "Render/model.h"                       // stbi_set_flip_vertically_on_load
#include "Render/GL/OpenGLRenderer.h"           // concrete backend + IRenderer + render types
#include "Core/Engine/input_manager.h"
#include "Core/Engine/SceneManager.h"
#include "Core/Engine/Services/Window.h"        // backend-aware GLFW wrapper
#include "Core/Engine/Services/InputBindings.h" // data-driven action -> key bindings
#include "Core/Engine/Services/FileWatcher.h"   // watch app.json for on-the-fly root-scene swap
#include "Core/Engine/Services/LoadContext.h"
#include "Core/Engine/Config/EngineSettings.h"  // window/render/backend defaults
#include "Core/Engine/Config/AppConfig.h"       // which root scene boots
#include "Core/Engine/Config/BuiltinComponents.h" // self-registers every component & script

// The application root. Owns the window, renderer, input and the frame loop, and drives
// the SceneManager. main() shrinks to: construct -> init() -> run().
//
// Access: main owns the Engine (stack), so teardown order is deterministic — but a
// static pointer (set in the ctor) lets scripts reach it via Engine::instance(), e.g. a
// GameManager calling Engine::instance().requestQuit() on an "Exit" action. NOTE: it is
// intentionally NOT a Meyers singleton — that would destroy the window/renderer at static
// exit, AFTER the GL context is gone (the same trap SceneManager has to work around).
class Engine {
public:
    Engine() : m_scenes(SceneManager::instance()) { s_instance = this; }
    ~Engine() { shutdown(); if (s_instance == this) s_instance = nullptr; }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    static Engine& instance() { return *s_instance; }

    // Build window/renderer/input from config and load the root scene. false on failure.
    bool init() {
        m_settings = EngineSettings::load(m_engineConfigPath);

        // Window first (it owns the GL context the renderer needs).
        m_window = std::make_unique<Window>(m_settings.window, m_settings.backend);
        if (!m_window->valid()) return false;
        m_window->captureCursor(true);

        m_renderer = createRenderer(m_settings.backend);
        m_renderer->applySettings(m_settings.render);
        if (!m_renderer->init(m_window->width(), m_window->height())) return false;

        stbi_set_flip_vertically_on_load(true);

        m_input = std::make_unique<InputManager>(m_window->handle());
        InputBindings::loadInto(*m_input, m_inputConfigPath);
        m_inputWatcher = std::make_unique<InputBindings::Watcher>(m_inputConfigPath);
        m_loadCtx.input = m_input.get();

        m_app = AppConfig::load(m_appConfigPath);
        m_appWatcher = std::make_unique<FileWatcher>(m_appConfigPath);

        m_scenes.loadManifest(m_app.rootScene, m_loadCtx);
        return true;
    }

    void run() {
        while (isRunning()) frame();
        shutdown();
    }

    // Clean-close entry point — callable from scripts via Engine::instance().requestQuit().
    // Only signals; the loop finishes the current frame and the ordered teardown runs.
    void requestQuit() { m_running = false; }
    bool isRunning() const { return m_running && m_window && !m_window->shouldClose(); }

    // Subsystem access for scripts/tools.
    Window&       window()   { return *m_window; }
    IRenderer&    renderer() { return *m_renderer; }
    InputManager& input()    { return *m_input; }
    SceneManager& scenes()   { return m_scenes; }

private:
    // The single place a concrete backend is named; everything else talks to IRenderer.
    static std::unique_ptr<IRenderer> createRenderer(RenderBackend backend) {
        switch (backend) {
            case RenderBackend::OpenGL:
                return std::make_unique<OpenGLRenderer>(Window::glLoader());
            case RenderBackend::Vulkan:
            default:
                std::cerr << "[Engine] requested backend not implemented yet; using OpenGL\n";
                return std::make_unique<OpenGLRenderer>(Window::glLoader());
        }
    }

    // One iteration of the loop.
    void frame() {
        const float now = static_cast<float>(Window::time());
        m_deltaTime = now - m_lastFrame;
        m_lastFrame = now;

        m_inputWatcher->poll(*m_input);   // hot-reload key bindings
        m_scenes.poll(m_loadCtx);         // hot-reload scenes on file change

        // Root scene can change on the fly: repoint to a different world if app.json did.
        if (m_appWatcher->changed()) {
            AppConfig fresh = AppConfig::load(m_appConfigPath);
            if (fresh.rootScene != m_app.rootScene) {
                m_app.rootScene = fresh.rootScene;
                m_scenes.switchManifest(m_app.rootScene, m_loadCtx);
            }
        }

        if (m_input->IsActionReleased("Quit"))         requestQuit();
        if (m_input->IsActionReleased("ReloadScenes")) m_scenes.reload(m_loadCtx);

        // Camera lives in the (re)loadable root objects — fetch each frame (no dangling).
        // TODO: multi-camera support — the active camera is hardcoded as "MainCamera";
        // generalize to a "primary camera" the scene/manager can select.
        GameObject* camera = m_scenes.findObjectInRoot("MainCamera");
        if (!camera) { m_window->pollEvents(); return; }

        // --- update ---
        m_scenes.update(m_deltaTime);
        ColliderComponent3D::tickWorld();

        // --- camera for this frame ---
        auto* cam = camera->getComponent<CameraComponent>();
        const RenderSettings& rs = m_renderer->settings();
        const float aspect = static_cast<float>(rs.renderWidth) / static_cast<float>(rs.renderHeight);

        CameraView cv;
        cv.view       = cam->viewMatrix();
        cv.projection = cam->projectionMatrix(aspect);
        cv.position   = cam->owner->getComponent<TransformComponent3D>()->Position;

        // --- lights (sun + point + cone, all data-driven) ---
        LightSet lights;
        m_scenes.collectLights(lights);

        // --- render ---
        m_renderer->onResize(m_window->width(), m_window->height());
        m_renderer->beginScene(cv);
        m_renderer->setLights(lights);
        m_scenes.draw(*m_renderer);
        m_renderer->endScene();

        m_input->ResetFrame();
        m_window->present();
        m_window->pollEvents();
    }

    // Free GPU-owning objects while the GL context is alive, in order: scene/loose objects
    // -> renderer -> window (which terminates GLFW). Idempotent so the dtor can call it too.
    void shutdown() {
        m_scenes.unloadAll();   // Mesh/Model dtors glDelete* while GL is alive
        m_renderer.reset();     // renderer FBOs/shaders/quad
        m_window.reset();       // ~Window -> glfwTerminate (after GL objects are gone)
    }

    inline static Engine* s_instance = nullptr;

    EngineSettings m_settings;
    AppConfig      m_app;
    LoadContext    m_loadCtx;
    SceneManager&  m_scenes;

    std::string m_engineConfigPath = "app/Engine/render.json";
    std::string m_appConfigPath    = "app/app.json";
    std::string m_inputConfigPath  = "app/Config/input.json";

    std::unique_ptr<Window>                 m_window;
    std::unique_ptr<IRenderer>              m_renderer;
    std::unique_ptr<InputManager>           m_input;
    std::unique_ptr<FileWatcher>            m_appWatcher;
    std::unique_ptr<InputBindings::Watcher> m_inputWatcher;

    float m_deltaTime = 0.0f;
    float m_lastFrame = 0.0f;
    bool  m_running   = true;
};
