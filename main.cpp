#include <iostream>
#include <cmath>
#include <memory>
#include <glad/glad.h> //glad goes first (for the GLADloadproc type)
#include <glfw3.h>

#include "Render/model.h"               // model loading (+ stb image: stbi_set_flip_vertically_on_load)
#include "Render/GL/OpenGLRenderer.h"   // concrete backend; also brings IRenderer + render types
#include "Core/Engine/input_manager.h"
#include "Core/Engine/GameObjectLoader.h"  // data-driven GameObject loading from JSON
#include "Core/Engine/SceneManager.h"      // singleton root manager: loads/owns/drives scenes
#include "Core/Engine/InputBindings.h"     // data-driven action -> key bindings from JSON

#include "Core/Components/GameObject.h"
#include "Core/Engine/BuiltinComponents.h"   // pulls in + self-registers every component & script

// TODO: continue the decouple — extract Window (GLFW) and Engine (the loop) so main
// collapses to ~6 lines. This pass moved all rendering behind IRenderer.

using namespace std;

//global attributes
unsigned int SCR_WIDTH = 1920;
unsigned int SCR_HEIGHT = 1080;

// Fixed internal render resolution. The scene renders at this size; the renderer
// scales it to the window (letterbox/pillarbox). Now lives in RenderSettings and is
// changeable at runtime via renderer->applySettings().
const int RENDER_WIDTH  = 1920;
const int RENDER_HEIGHT = 1080;

//time
float deltaTime = 0.0f;	// Time between current frame and last frame
float lastFrame = 0.0f; // Time of last frame

// resize: only tracks the window size. The renderer's present pass reads it each
// frame via onResize(), so we don't touch any GL state here.
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    SCR_WIDTH = width;
    SCR_HEIGHT = (height == 0) ? 1 : height;
}

int main()
{
    //initialize glfw
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    //create window
    //TODO: add fullscreen option - glfwGetPrimaryMonitor() instead of NULL
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "OpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); //disable vsync

    // Renderer: the ONLY place the concrete GL backend is named. Everything below
    // talks to the IRenderer interface, so swapping backends is a one-line change.
    std::unique_ptr<IRenderer> renderer =
        std::make_unique<OpenGLRenderer>((GLADloadproc)glfwGetProcAddress);

    // Resolution + anti-aliasing are runtime-tunable: change these fields and call
    // renderer->applySettings(...) again at any time to rebuild the targets.
    RenderSettings rs;
    rs.renderWidth  = RENDER_WIDTH;
    rs.renderHeight = RENDER_HEIGHT;
    rs.aa           = AntiAliasing::MSAA;
    rs.msaaSamples  = 2;
    renderer->applySettings(rs);

    if (!renderer->init(SCR_WIDTH, SCR_HEIGHT)) {   // loads GLAD, builds targets + shaders
        glfwTerminate();
        return -1;
    }

    stbi_set_flip_vertically_on_load(true);

    //resize + mouse capture
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    InputManager input(window);
    const std::string inputConfig = "assets/config/input.json";
    InputBindings::loadInto(input, inputConfig);        // controls are data now
    InputBindings::Watcher inputWatcher(inputConfig);   // re-applies the file when it changes on disk
    //TODO: bind AA/resolution toggles here and call renderer->applySettings() at runtime.

    // ===== Scenes =====
    // The whole world is data-driven. A manifest (scenes.json) lists which scene files
    // to load; the SceneManager is the single root that loads/owns/drives them all and
    // is the intermediary scenes use to reach each other. Both the manifest and every
    // scene file are watched — edit one and it hot-reloads (or press the Reload key).
    LoadContext loadCtx;
    loadCtx.input = &input;
    SceneManager& scenes = SceneManager::instance();
    scenes.loadManifest("assets/scenes/scenes.json", loadCtx);
   
    // Camera is a loose/root object declared in the manifest's "objects" list (loaded by
    // loadManifest above). It's not part of any scene, so it persists across scene
    // hot-reloads and drives the view; CameraFollow resolves its "Player" target by name.
    GameObject* camera = scenes.findObjectInRoot("MainCamera");
    if (!camera) { std::cerr << "No 'MainCamera' root object in the manifest\n"; glfwTerminate(); return -1; }

    bool flashlightOn = false;

    //main loop — the more you can take out of the loop the better
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        inputWatcher.poll(input);   // live-reload controls if assets/config/input.json changed
        scenes.poll(loadCtx);       // auto hot-reload scenes if the manifest / a scene file changed

        if (input.IsActionReleased("Quit"))             glfwSetWindowShouldClose(window, true);
        if (input.IsActionReleased("ToggleFlashlight")) flashlightOn = !flashlightOn;
        if (input.IsActionReleased("ReloadScenes"))     scenes.reload(loadCtx);   // manual reload button

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

        // Camera flashlight: a spot that tracks the eye. It's just another cone light.
        SpotLight flashlight;
        flashlight.position    = cv.position;
        flashlight.direction   = cam->forward();
        flashlight.cutOff      = glm::cos(glm::radians(12.5f));
        flashlight.outerCutOff = glm::cos(glm::radians(17.5f));
        flashlight.constant    = 1.0f;
        flashlight.linear      = 0.09f;
        flashlight.quadratic   = 0.032f;
        flashlight.diffuse     = glm::vec3(1.0f);
        flashlight.specular    = glm::vec3(1.0f);
        flashlight.enabled     = flashlightOn;
        lights.spots.push_back(flashlight);

        // --- render ---
        renderer->onResize(SCR_WIDTH, SCR_HEIGHT);
        renderer->beginScene(cv);
        renderer->setLights(lights);

        // Each active scene submits its objects' meshes (own material + scene offset).
        scenes.draw(*renderer);

        renderer->endScene();

        input.ResetFrame();

        //check all events and swap buffers
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // cleanup: release GPU-owning objects while the GL context is still alive.
    // Mesh/Model dtors call glDelete*; the renderer deletes its FBOs/shaders/quad.
    // unloadAll() MUST run here (not at static destruction of the singleton, which
    // happens after the GL context is gone) so those destructors see a live context.
    scenes.unloadAll();   // frees scenes + loose objects (the camera) while GL is alive
    renderer.reset();

    glfwTerminate();
    return 0;
}
