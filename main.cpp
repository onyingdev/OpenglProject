#include <iostream>
#include <cmath>
#include <memory>
#include <glad/glad.h> //glad goes first (for the GLADloadproc type)
#include <glfw3.h>

#include "Render/model.h"               // model loading (+ stb image: stbi_set_flip_vertically_on_load)
#include "Render/GL/OpenGLRenderer.h"   // concrete backend; also brings IRenderer + render types
#include "Core/Engine/input_manager.h"
#include "Core/Engine/GameObjectLoader.h"  // data-driven GameObject loading from JSON
#include "Core/Engine/SceneLoader.h"       // data-driven Scene loading from JSON
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

    // ===== Scene =====
    // The whole world is now data-driven: one scene file lists its objects, each of
    // which is its own GameObject JSON. The Scene owns those objects and applies its
    // transform to all of them. start() is deferred to here so every object exists
    // before any script runs; starting the root starts the Scene and its children.
    LoadContext loadCtx;
    loadCtx.input = &input;
    auto sceneRoot = SceneLoader::fromFile("assets/scenes/level1.json", loadCtx);
    if (!sceneRoot) { std::cerr << "Failed to load level1.json\n"; glfwTerminate(); return -1; }
    sceneRoot->start();

    Scene* scene = sceneRoot->getComponent<Scene>();
    GameObject* player = scene->getObject("Player");   // borrowed; Scene owns it
    if (!player) {
        std::cerr << "Scene is missing the Player object\n";
        glfwTerminate(); return -1;
    }

    // Camera stays outside the scene: it references the player and drives the view.
    auto camera = std::make_unique<GameObject>();
    camera->Name = "MainCamera";
    camera->addComponent<TransformComponent3D>()->Position = {-1, 1, 0}; 
    camera->addComponent<InputComponent>(&input);
    camera->addComponent<CameraComponent>();
    camera->addComponent<CameraFollow>(); //script
    camera->getComponent<CameraFollow>()->Target = player;
    camera->start();

    bool flashlightOn = false;

    //main loop — the more you can take out of the loop the better
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        inputWatcher.poll(input);   // live-reload controls if assets/config/input.json changed

        if (input.IsActionReleased("Quit"))             glfwSetWindowShouldClose(window, true);
        if (input.IsActionReleased("ToggleFlashlight")) flashlightOn = !flashlightOn;

        // --- update simulation ---
        camera->update(deltaTime);
        sceneRoot->update(deltaTime);   // updates the Scene -> all its objects
        ColliderComponent3D::tickWorld();

        // --- gather camera for this frame ---
        auto* cam = camera->getComponent<CameraComponent>();
        const RenderSettings& settings = renderer->settings();
        const float aspect = (float)settings.renderWidth / (float)settings.renderHeight;

        CameraView cv;
        cv.view       = cam->viewMatrix();
        cv.projection = cam->projectionMatrix(aspect);
        cv.position   = cam->owner->getComponent<TransformComponent3D>()->Position;

        // --- gather lights from the scene: the directional sun, point lamps, and
        // cone lights are all data now (kDefaultLightColor unless the object assigns
        // one). The flashlight below is a camera spot added on top. ---
        LightSet lights;
        for (auto& [name, obj] : scene->objects()) {
            if (auto* dl = obj->getComponent<DirectionalLightComponent>())
                lights.dir = dl->toDirLight();     // one sun; last wins if several
            if (auto* pl = obj->getComponent<PointLightComponent>())
                lights.points.push_back(pl->toPointLight());
            if (auto* cl = obj->getComponent<ConeLightComponent>())
                lights.spots.push_back(cl->toSpotLight());
        }

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

        // The Scene submits every object's mesh, each with its own material (the fish
        // carry "unlit" + color in their JSON now) and offset by the scene transform.
        scene->draw(*renderer);

        renderer->endScene();

        input.ResetFrame();

        //check all events and swap buffers
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // cleanup: release GPU-owning objects while the GL context is still alive.
    // Mesh/Model dtors call glDelete*; the renderer deletes its FBOs/shaders/quad.
    // All must run before glfwTerminate(). unload() frees every scene object; the
    // scene root and camera then release their own components.
    scene->unload();
    sceneRoot.reset();
    camera.reset();
    renderer.reset();

    glfwTerminate();
    return 0;
}
