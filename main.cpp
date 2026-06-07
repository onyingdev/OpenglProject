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
    InputBindings::loadInto(input, "assets/config/input.json");   // controls are data now
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
    GameObject* fish   = scene->getObject("Fish1");    // the two lamp fish, for point lights
    GameObject* fish2  = scene->getObject("Fish2");
    if (!player || !fish || !fish2) {
        std::cerr << "Scene is missing a required object (Player/Fish1/Fish2)\n";
        glfwTerminate(); return -1;
    }
    const glm::vec3 lightColor (1.0f, 0.0f, 0.0f);   // matches Fish1's unlit color
    const glm::vec3 lightColor2(0.0f, 0.0f, 0.5f);   // matches Fish2's unlit color

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

    // Build a point light from a lamp's position + color (mirrors the old setPointLight).
    auto makePoint = [](const glm::vec3& pos, const glm::vec3& color) {
        PointLight pl;
        pl.position  = pos;
        pl.constant  = 1.0f; pl.linear = 0.09f; pl.quadratic = 0.032f;
        pl.ambient   = 0.05f * color;
        pl.diffuse   = color;
        pl.specular  = color;
        pl.enabled   = true;
        return pl;
    };

    //main loop — the more you can take out of the loop the better
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

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

        // --- gather lights: two fish are point lamps, flashlight is a camera spot ---
        const glm::vec3 lightPos  = fish->getComponent<TransformComponent3D>()->Position;
        const glm::vec3 lightPos2 = fish2->getComponent<TransformComponent3D>()->Position;

        LightSet lights;
        lights.dir.direction = {-0.2f, -1.0f, -0.3f};
        lights.dir.ambient   = { 0.05f, 0.05f, 0.05f};
        lights.dir.diffuse   = { 0.30f, 0.30f, 0.30f};
        lights.dir.specular  = { 0.40f, 0.40f, 0.40f};
        lights.points.push_back(makePoint(lightPos,  lightColor));
        lights.points.push_back(makePoint(lightPos2, lightColor2));

        lights.spot.position    = cv.position;
        lights.spot.direction   = cam->forward();
        lights.spot.cutOff      = glm::cos(glm::radians(12.5f));
        lights.spot.outerCutOff = glm::cos(glm::radians(17.5f));
        lights.spot.constant    = 1.0f;
        lights.spot.linear      = 0.09f;
        lights.spot.quadratic   = 0.032f;
        lights.spot.diffuse     = glm::vec3(1.0f);
        lights.spot.specular    = glm::vec3(1.0f);
        lights.spot.enabled     = flashlightOn;

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
