#include <iostream>
#include <cmath>
#include <memory>
#include "vendor/glad/glad/glad.h" //glad goes first
#include "vendor/GLFW/include/glfw3.h"
#include "include/Render/shader_s.h"
//#include "assets/scripts/camera.h"

//#define STB_IMAGE_IMPLEMENTATION
//#include "assets/textures/stb_image.h"
//#include "include/Core/lib_asset_loader.h"
#include "include/Render/model.h"
#include "include/Core/input_manager.h"
#include "include/Core/Component.h"
#include "include/Core/GameObject.h"
#include "include/Components/TransformComponent3D.h"
#include "include/Components/InputComponent.h"
#include "include/Components/CameraComponent.h"
#include "include/Components/MeshComponent.h"
#include "include/Components/CharacterComponent3D.h"
#include "include/Components/ColliderComponent3D.h"

#include "assets/scripts/cameraController.h"
#include "assets/scripts/characterController.h"
#include "assets/scripts/spinModel.h"
#include "assets/scripts/cameraFollow.h"

//TODO: refactor with main to decouple functions

using namespace std;

//global attributes
unsigned int SCR_WIDTH = 1920;
unsigned int SCR_HEIGHT = 1080;

// Fixed internal render resolution. The scene always renders at this size,
// then is scaled to fit the window (with letterbox/pillarbox to preserve aspect).
const int RENDER_WIDTH  = 1920;
const int RENDER_HEIGHT = 1080;

//time
float deltaTime = 0.0f;	// Time between current frame and last frame
float lastFrame = 0.0f; // Time of last frame

//resize: only tracks the window size now. The viewport is set per-pass in the
//render loop (FBO pass uses RENDER_*, blit pass uses SCR_*), so we don't call
//glViewport here anymore.
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    SCR_WIDTH = width;
    SCR_HEIGHT = (height == 0) ? 1 : height;
}

// TODO: Apply an Adapter Pattern for Graphic API. We want to migrate to Vulkan or any rendering API in the near future.
int main()
{
    //initialize glfw
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); //MAC OS
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    //create window
    //TODO: add fullscreen option - uncomment to enable: glfwGetPrimaryMonitor() instead of NULL
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "OpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval( 0 ); //disable vsync
  
    //initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    stbi_set_flip_vertically_on_load(true);

    //resize
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // Offscreen framebuffer (internal resolution)
    // Scene draws into this FBO at RENDER_WIDTH x RENDER_HEIGHT, then we blit
    // it to the window. The blit handles scaling so the window size never
    // affects render cost or aspect.
    unsigned int fbo, fboColor, fboDepth;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color attachment — the texture the scene rasterises into.
    glGenTextures(1, &fboColor);
    glBindTexture(GL_TEXTURE_2D, fboColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, RENDER_WIDTH, RENDER_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // GL_NEAREST for pixel-art
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColor, 0);

    // Depth attachment — renderbuffer is cheaper than a depth texture since we don't sample it.
    glGenRenderbuffers(1, &fboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, fboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, RENDER_WIDTH, RENDER_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR: Offscreen framebuffer is incomplete\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    //mouse capture
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    //glfwSetCursorPosCallback(window, mouse_callback);
    if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

    InputManager input(window);
    input.BindAction("Quit", GLFW_KEY_ESCAPE);
    // input.BindAction("CameraMoveForward", GLFW_KEY_W);
    // input.BindAction("CameraMoveBackward", GLFW_KEY_S);
    // input.BindAction("CameraMoveLeft", GLFW_KEY_A);
    // input.BindAction("CameraMoveRight", GLFW_KEY_D);
    // input.BindAction("CameraMoveUp", GLFW_KEY_Q);
    // input.BindAction("CameraMoveDown", GLFW_KEY_E);
    input.BindAction("MoveBackward", GLFW_KEY_S);
    input.BindAction("MoveForward", GLFW_KEY_W);
    input.BindAction("MoveRight", GLFW_KEY_D);
    input.BindAction("MoveLeft", GLFW_KEY_A);
    input.BindAction("RotateRight", GLFW_KEY_M);
    input.BindAction("RotateLeft", GLFW_KEY_N);



    //TODO: add ToggleFullscreen action (F11) with glfwSetWindowMonitor

    auto playground = std::make_unique<GameObject>();
    playground->Name = "playground";
    playground->addComponent<TransformComponent>()->Position = {0, 0, 0};
    playground->addComponent<MeshComponent>("assets/models/test_playground/testPlayground.obj");
    //playground->getComponent<TransformComponent>()->Scale = {0.2f, 0.2f, 0.2f}; 
    playground->start();


    auto player = std::make_unique<GameObject>();
    player->Name = "Player";
    player->addComponent<TransformComponent>()->Position = {0, 0, 0};
    player->addComponent<MeshComponent>("assets/models/nier/2B.obj");
    player->addComponent<InputComponent>(&input);
    player->addComponent<CharacterComponent>();
    player->addComponent<CharacterController>();
    player->addComponent<ColliderComponent3D>();
    //player->addComponent<SpinModel>();
    player->start();

    auto fish = std::make_unique<GameObject>();
    fish->Name = "Fish";
    fish->addComponent<TransformComponent>()->Position = {5, 0, 0};
    fish->addComponent<MeshComponent>("assets/models/fish1/fish1.obj");
    fish->addComponent<ColliderComponent3D>();
    fish->addComponent<SpinModel>();
    fish->start();

    auto camera = std::make_unique<GameObject>();
    camera->Name = "MainCamera";
    camera->addComponent<TransformComponent>();
    //camera->addComponent<TransformComponent>()->owner = player.get(); 
    camera->addComponent<CameraComponent>();
    camera->addComponent<CameraFollow>(); //script
    camera->getComponent<CameraFollow>()->Target = player.get();
    camera->start();



    Shader shaderProgram("assets/shaders/vertex_s.vs", "assets/shaders/fragment_s.fs");
    // Model ourModel("assets/models/nier/2B.obj"); //TODO: change path loading system


    //main loop the more you can take out of the loop the better
    while(!glfwWindowShouldClose(window))
    {
        //calculate physics
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (input.IsActionReleased("Quit")) {
            glfwSetWindowShouldClose(window, true);
        }

        // drive all components on the camera (movement + mouse look)
        camera->update(deltaTime);

        // Pass 1, render the scene into the offscreen FBO
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, RENDER_WIDTH, RENDER_HEIGHT);
        glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shaderProgram.use();

        auto* cam = camera->getComponent<CameraComponent>(); //get camera component might be null
        glm::mat4 view = cam->viewMatrix();
        // Aspect ratio is fixed by the internal resolution now, NOT the window.
        glm::mat4 proj = cam->projectionMatrix((float)RENDER_WIDTH / (float)RENDER_HEIGHT);
        shaderProgram.setMat4("view", view);
        shaderProgram.setMat4("projection", proj);

        player->update(deltaTime);
        player->getComponent<ColliderComponent3D>()->tickWorld(); // check collisions before drawing
        player->getComponent<MeshComponent>()->draw(shaderProgram);  
        playground->update(deltaTime);
        playground->getComponent<MeshComponent>()->draw(shaderProgram);  
        fish->update(deltaTime);
        fish->getComponent<ColliderComponent3D>()->tickWorld(); // check collisions before drawing
        fish->getComponent<MeshComponent>()->draw(shaderProgram);
        // cube->update(deltaTime);
        // cube->getComponent<MeshComponent>()->draw(shaderProgram);  

        // Pass 2, blit FBO to window, preserving aspect (letterbox/pillarbox)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        // Clear the window first so the unused area shows as black bars.
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Compute destination rect that fits the render aspect inside the window.
        const float renderAspect = (float)RENDER_WIDTH  / (float)RENDER_HEIGHT;
        const float windowAspect = (float)SCR_WIDTH     / (float)SCR_HEIGHT;
        int dx, dy, dw, dh;
        if (windowAspect > renderAspect) {
            // window is wider than render → pillarbox (black bars on left/right)
            dh = SCR_HEIGHT;
            dw = (int)(SCR_HEIGHT * renderAspect);
            dx = ((int)SCR_WIDTH - dw) / 2;
            dy = 0;
        } else {
            // window is taller than render → letterbox (black bars on top/bottom)
            dw = SCR_WIDTH;
            dh = (int)(SCR_WIDTH / renderAspect);
            dx = 0;
            dy = ((int)SCR_HEIGHT - dh) / 2;
        }
        glBlitFramebuffer(0, 0, RENDER_WIDTH, RENDER_HEIGHT,
                          dx, dy, dx + dw, dy + dh,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        input.ResetFrame();

        //check all events and swap buffer
        glfwSwapBuffers(window);
        glfwPollEvents();    //SO events like key, mouse, etc
    }

    // cleanup
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &fboColor);
    glDeleteRenderbuffers(1, &fboDepth);

    glfwTerminate();
    return 0;
}