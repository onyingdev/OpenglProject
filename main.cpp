#include <iostream>
#include <cmath>
#include <memory>
#include <glad/glad.h> //glad goes first (for the GLADloadproc type)

#include "Render/model.h"               // model loading (+ stb image: stbi_set_flip_vertically_on_load)
#include "Render/GL/OpenGLRenderer.h"   // concrete backend; also brings IRenderer + render types
#include "Core/Engine/input_manager.h"
#include "Core/Engine/Services/GameObjectLoader.h"  // data-driven GameObject loading from JSON
#include "Core/Engine/SceneManager.h"      // singleton root manager: loads/owns/drives scenes
#include "Core/Engine/Services/InputBindings.h"     // data-driven action -> key bindings from JSON
#include "Core/Engine/Config/EngineSettings.h"    // window/render/backend defaults from app/Engine
#include "Core/Engine/Services/Window.h"            // backend-aware GLFW wrapper (all GLFW lives here)
#include "Core/Engine/Config/AppConfig.h"         // root-scene (manifest) path — app/app.json
#include "Core/Engine/Services/FileWatcher.h"       // watch app.json to repoint the root scene live

#include "Core/Components/GameObject.h"
#include "Core/Engine/Config/BuiltinComponents.h"   // pulls in + self-registers every component & script

// TODO: next decouple step — extract an Engine (the loop) so main collapses to ~3 lines.
// This pass moved all GLFW behind Window and made window/render/backend data-driven.
// TODO: Add multicamera support.

using namespace std;

//time
float deltaTime = 0.0f;	// Time between current frame and last frame
float lastFrame = 0.0f; // Time of last frame

// Build the renderer for the configured backend. The ONLY place a concrete backend is
// named; everything else talks to IRenderer. (Vulkan/other slot in here later.)
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

int main()
{
    // Engine/graphics settings: defaults from app/Engine, optionally overlaid by a
    // player settings file (swappable path) — window size, mode, backend, AA, etc.
    EngineSettings settings = EngineSettings::load("app/Engine/render.json");

    // Window: the only place GLFW lives now. Backend-aware (GL context vs Vulkan NO_API).
    Window window(settings.window, settings.backend);
    if (!window.valid()) { return -1; }   // Window already logged + cleaned up GLFW
    window.captureCursor(true);

    // Renderer for the configured backend; everything below talks to IRenderer.
    std::unique_ptr<IRenderer> renderer = createRenderer(settings.backend);
    renderer->applySettings(settings.render);                       // internal res + AA
    if (!renderer->init(window.width(), window.height())) {         // loads GLAD, builds targets
        return -1;
    }

    stbi_set_flip_vertically_on_load(true);

    InputManager input(window.handle());
    const std::string inputConfig = "app/Config/input.json";
    InputBindings::loadInto(input, inputConfig);        // controls are data now
    InputBindings::Watcher inputWatcher(inputConfig);   // re-applies the file when it changes on disk
    //TODO: bind AA/resolution toggles here and call renderer->applySettings() at runtime.

    // ===== Scenes =====
    // The root scene (which manifest to boot) is data, not hardcoded: app/app.json names
    // it, and the file is watched so editing rootScene swaps the whole world on the fly.
    // The SceneManager loads/owns/drives the scenes + loose objects the manifest lists.
    LoadContext loadCtx;
    loadCtx.input = &input;
    const std::string appConfigPath = "app/app.json";
    AppConfig   app = AppConfig::load(appConfigPath);
    FileWatcher appWatcher(appConfigPath);

    SceneManager& scenes = SceneManager::instance();
    scenes.loadManifest(app.rootScene, loadCtx);

    //main loop — the more you can take out of the loop the better
    while (!window.shouldClose())
    {
        float currentFrame = static_cast<float>(Window::time());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        inputWatcher.poll(input);   // live-reload controls if app/Config/input.json changed
        scenes.poll(loadCtx);       // auto hot-reload scenes if the manifest / a scene file changed

        // Root scene can change on the fly: if app.json's rootScene now points elsewhere,
        // switch the whole world to the new manifest.
        if (appWatcher.changed()) {
            AppConfig fresh = AppConfig::load(appConfigPath);
            if (fresh.rootScene != app.rootScene) {
                app.rootScene = fresh.rootScene;
                scenes.switchManifest(app.rootScene, loadCtx);
            }
        }

        if (input.IsActionReleased("Quit"))             window.requestClose();    // manual quit button, debug.
        if (input.IsActionReleased("ReloadScenes"))     scenes.reload(loadCtx);   // manual reload button, debug.

        // Camera lives in the (re)loadable root objects, so fetch it each frame — robust
        // to scene reloads and root-scene switches (no dangling cached pointer).
        GameObject* camera = scenes.findObjectInRoot("MainCamera");
        if (!camera) { window.pollEvents(); continue; }   // none yet (e.g. bad manifest)

        // --- update simulation ---
        scenes.update(deltaTime);   // updates every active scene + loose objects (the camera)
        ColliderComponent3D::tickWorld();

        // --- gather camera for this frame ---
        auto* cam = camera->getComponent<CameraComponent>();
        const RenderSettings& settings = renderer->settings();
        const float aspect = (float)settings.renderWidth / (float)settings.renderHeight;

        CameraView cv;
        cv.view       = cam->viewMatrix();
        cv.projection = cam->projectionMatrix(aspect);
        cv.position   = cam->owner->getComponent<TransformComponent3D>()->Position;

        // --- gather lights from every active scene: the directional sun, point lamps,
        // and cone lights are all data (kDefaultLightColor unless the object assigns
        // one). The flashlight below is a camera spot added on top. ---
        LightSet lights;
        scenes.collectLights(lights);

        // --- render ---
        renderer->onResize(window.width(), window.height());
        renderer->beginScene(cv);
        renderer->setLights(lights);

        // Each active scene submits its objects' meshes (own material + scene offset).
        scenes.draw(*renderer);

        renderer->endScene();

        input.ResetFrame();

        // present the finished frame + pump OS events
        window.present();
        window.pollEvents();
    }

    // cleanup: release GPU-owning objects while the GL context is still alive.
    // Mesh/Model dtors call glDelete*; the renderer deletes its FBOs/shaders/quad.
    // unloadAll() MUST run here (not at static destruction of the singleton, which
    // happens after the GL context is gone) so those destructors see a live context.
    // Order: scenes + renderer go first; `window` (declared earlier) is destroyed last
    // at scope exit, terminating GLFW after the GL objects are already gone.
    scenes.unloadAll();   // frees scenes + loose objects (the camera) while GL is alive
    renderer.reset();
    return 0;
}
