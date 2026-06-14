#pragma once
#include <memory>
#include <string>
#include <iostream>

#include "Render/model.h"                       // stbi_set_flip_vertically_on_load
#include "Render/ModelCache.h"                  // shared model cache — cleared at shutdown (GL alive)
#include "Render/Assets/AssetManager.h"         // async loader: per-frame upload pump + worker teardown
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
#include "Components/Physics/ColliderDebugDraw.h"  // debugDrawColliders() wireframe pass
#include "Components/Physics/RigidBodyComponent3D.h" // DebugTimings/DebugSolver/DebugPrint statics
#include "Core/Engine/Physics/physics_manager.h"   // drives the v2 physics step
#include "Core/Engine/Profiling/Profiler.h"        // per-stage frame timing (CPU + GPU, toggled by config)

// The application root. Owns the window, renderer, input and the frame loop, and drives
// the SceneManager. main() shrinks to: construct -> init() -> run().
//
// Access: main owns the Engine (stack), so teardown order is deterministic — but a
// static pointer (set in the ctor) lets scripts reach it via Engine::instance(), e.g. a
// GameManager calling Engine::instance().requestQuit() on an "Exit" action. NOTE: it is
// intentionally NOT a Meyers singleton — that would destroy the window/renderer at static
// exit, AFTER the GL context is gone (the same trap SceneManager has to work around).
// Engine manages the creation/destruction order, also manages the creation of the frame loop.
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
        m_debugColliders = m_settings.render.debugColliders;   // initial state (key toggles at runtime)

        // Profiler toggles are data-driven (render.json), so timing can be turned on without a rebuild.
        Profiler::CpuEnabled    = m_settings.profiler.cpu;
        Profiler::GpuEnabled    = m_settings.profiler.gpu;
        Profiler::DetailEnabled = m_settings.profiler.detail;
        Profiler::ReportEvery   = m_settings.profiler.reportEvery;

        // Physics step self-profiling (its own [phys2] prints), driven from the same config block.
        RigidBodyComponent3D::DebugTimings = m_settings.profiler.physTimings;
        RigidBodyComponent3D::DebugSolver  = m_settings.profiler.physSolver;
        RigidBodyComponent3D::DebugPrint   = m_settings.profiler.physPrint;

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

        if (m_input->IsActionReleased("Quit"))            requestQuit(); //debug
        if (m_input->IsActionReleased("ReloadScenes"))    m_scenes.reload(m_loadCtx); //debug
        if (m_input->IsActionReleased("ToggleColliders")) m_debugColliders = !m_debugColliders; //debug

        // Camera lives in the (re)loadable root objects — fetch each frame (no dangling).
        // TODO: multi-camera support — the active camera is hardcoded as "MainCamera";
        // generalize to a "primary camera" the scene/manager can select.
        GameObject* camera = m_scenes.findObjectInRoot("MainCamera");
        if (!camera) { m_window->pollEvents(); return; }

        // --- update ---
        { PROFILE_CPU("update");
        m_scenes.update(m_deltaTime);          // scripts (variable dt) gather forces via AddForce/AddImpulse
        }

        // Physics on a fixed timestep: accumulate real dt and step in constant slices so
        // integration/resolution stay stable regardless of framerate. Leftover carries to
        // the next frame; kMaxSubSteps caps catch-up to avoid a spiral of death.
        { PROFILE_CPU("physics");   // wall-time of the substep loop; [phys2] still prints its own breakdown
        m_physicsAccumulator += m_deltaTime;
        int steps = 0;
        while (m_physicsAccumulator >= kFixedDt && steps < kMaxSubSteps) {
            OldRigidBodyComponent3D::stepWorld(kFixedDt);
            PhysicsManager::instance().step(kFixedDt);   // v2 world: manager owns the bodies + drives the step
            m_physicsAccumulator -= kFixedDt;
            ++steps;
        }
        if (steps == kMaxSubSteps) m_physicsAccumulator = 0.0f;   // drop backlog if we fell behind
        }

        { PROFILE_CPU("hurtbox");
        HurtBoxComponent3D::tickWorld();       // gameplay triggers once/frame on post-physics positions
        }

        // Upload models that finished parsing on the loader threads (GL work — must be on this thread).
        // Time-budgeted so a big batch spreads across frames; newly uploaded models render this frame.
        { PROFILE_CPU("assetPump");
        AssetManager::instance().pump();
        }

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
        { PROFILE_CPU("collectLights");
        m_scenes.collectLights(lights);
        }

        // --- render ---
        m_renderer->onResize(m_window->width(), m_window->height());
        { PROFILE_CPU("beginScene"); m_renderer->beginScene(cv); }
        { PROFILE_CPU("setLights");  m_renderer->setLights(lights); }
        { PROFILE_CPU("draw");       m_scenes.draw(*m_renderer); }
        { PROFILE_CPU("debugDraw");  if (m_debugColliders) debugDrawColliders(*m_renderer); }  // collider/hurtbox wireframes
        { PROFILE_CPU("endScene");   m_renderer->endScene(); }

        m_input->ResetFrame();
        { PROFILE_CPU("present"); m_window->present(); }
        m_window->pollEvents();

        Profiler::instance().endFrame();   // resolve GPU queries + averaged report (no-op when disabled)
    }

    // Free GPU-owning objects while the GL context is alive, in order: scene/loose objects
    // -> renderer -> window (which terminates GLFW). Idempotent so the dtor can call it too.
    void shutdown() {
        m_scenes.unloadAll();   // Mesh/Model dtors glDelete* while GL is alive
        AssetManager::instance().shutdown();  // join loader threads + drop cached models while GL alive
        Profiler::instance().shutdownGL();   // glDeleteQueries while the context is alive
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
    bool  m_debugColliders = false;   // wireframe gizmos; initial state from render.json, toggled by key

    // Fixed-timestep physics: step in 1/60s slices, capped at kMaxSubSteps catch-up steps.
    float                m_physicsAccumulator = 0.0f;
    static constexpr float kFixedDt      = 1.0f / 60.0f;
    static constexpr int   kMaxSubSteps  = 5;
};
