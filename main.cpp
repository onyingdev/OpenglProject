#include <iostream>
#include <cmath>
#include <memory>
#include <glad/glad.h> //glad goes first (for the GLADloadproc type)
#include <glfw3.h>

#include "Render/model.h"               // model loading (+ stb image: stbi_set_flip_vertically_on_load)
#include "Render/GL/OpenGLRenderer.h"   // concrete backend; also brings IRenderer + render types
#include "Core/Engine/input_manager.h"
#include "Core/Engine/GameObjectLoader.h"  // data-driven GameObject loading from JSON

#include "Core/Components/Component.h"
#include "Core/Components/GameObject.h"
#include "Core/Components/TransformComponent3D.h"

#include "Components/Input/InputComponent.h"
#include "Components/Rendering/CameraComponent.h"
#include "Components/Rendering/MeshComponent.h"
#include "Components/Gameplay/CharacterComponent3D.h"
#include "Components/Physics/ColliderComponent3D.h"

#include "assets/scripts/cameraController.h"
#include "assets/scripts/characterController.h"
#include "assets/scripts/spinModel.h"
#include "assets/scripts/cameraFollow.h"

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
    input.BindAction("Quit", GLFW_KEY_ESCAPE);
    input.BindAction("MoveBackward", GLFW_KEY_DOWN);
    input.BindAction("MoveForward", GLFW_KEY_UP);
    input.BindAction("MoveRight", GLFW_KEY_RIGHT);
    input.BindAction("MoveLeft", GLFW_KEY_LEFT);
    input.BindAction("RotateRight", GLFW_KEY_M);
    input.BindAction("RotateLeft", GLFW_KEY_N);
    input.BindAction("ToggleFlashlight", GLFW_KEY_F);
    //TODO: bind AA/resolution toggles here and call renderer->applySettings() at runtime.

    //TODO: Create scene loader. Injects the objects from a JSON file.
    // ===== Scene =====
    auto playground = std::make_unique<GameObject>();
    playground->Name = "playground";
    playground->addComponent<TransformComponent3D>()->Position = {0, -1, 0};
    playground->addComponent<MeshComponent>("assets/models/test_playground/testPlayground.obj");
    playground->start();

    // Player is now data-driven: built from assets/gameObjects/player.json by the
    // reusable GameObject loader (the same call a SceneLoader will use per object).
    // start() is deferred to here so the whole object exists before its scripts run.
    LoadContext loadCtx;
    loadCtx.input = &input;
    auto player = GameObjectLoader::fromFile("assets/gameObjects/player.json", loadCtx);
    if (!player) { std::cerr << "Failed to load player.json\n"; glfwTerminate(); return -1; }
    player->start();

    auto fish = GameObjectLoader::fromFile("assets/gameObjects/fish1.json", loadCtx);
    if (!fish) { std::cerr << "Failed to load fish1.json\n"; glfwTerminate(); return -1; }
    fish->start();
    const glm::vec3 lightColor(1.0f, 0.0f, 0.0f);

    auto fish2 = std::make_unique<GameObject>();
    fish2->Name = "Fish2";
    fish2->addComponent<TransformComponent3D>()->Position = {-2, 0, 0};
    fish2->addComponent<MeshComponent>("assets/models/fish1/fish1.obj");
    fish2->addComponent<ColliderComponent3D>()->Shape = std::make_unique<SphereShape>(0.5f);
    fish2->addComponent<SpinModel>();
    fish2->start();
    const glm::vec3 lightColor2(0.0f, 0.0f, 0.5f);

    auto camera = std::make_unique<GameObject>();
    camera->Name = "MainCamera";
    camera->addComponent<TransformComponent3D>()->Position = {-1, 1, 0};
    camera->addComponent<InputComponent>(&input);
    camera->addComponent<CameraComponent>();
    camera->addComponent<CameraFollow>(); //script
    camera->getComponent<CameraFollow>()->Target = player.get();
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
        player->update(deltaTime);
        fish->update(deltaTime);
        fish2->update(deltaTime);
        playground->update(deltaTime);
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

        // Fish are unlit lamps (drawn in their own color); player + playground are lit.
        RenderItem fishItem = fish->getComponent<MeshComponent>()->renderItem();
        fishItem.material = MaterialKind::Unlit;  fishItem.unlitColor = lightColor;
        renderer->submit(fishItem);

        RenderItem fish2Item = fish2->getComponent<MeshComponent>()->renderItem();
        fish2Item.material = MaterialKind::Unlit;  fish2Item.unlitColor = lightColor2;
        renderer->submit(fish2Item);

        renderer->submit(player->getComponent<MeshComponent>()->renderItem());
        renderer->submit(playground->getComponent<MeshComponent>()->renderItem());

        renderer->endScene();

        input.ResetFrame();

        //check all events and swap buffers
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // cleanup: release GPU-owning objects while the GL context is still alive.
    // Mesh/Model dtors call glDelete*; the renderer deletes its FBOs/shaders/quad.
    // All must run before glfwTerminate().
    playground.reset();
    player.reset();
    fish.reset();
    fish2.reset();
    camera.reset();
    renderer.reset();

    glfwTerminate();
    return 0;
}
