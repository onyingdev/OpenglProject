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
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_MULTISAMPLE);   

    // MSAA samples per pixel for the offscreen scene FBO.
    const int MSAA_SAMPLES = 2;

    // Pass 1 target: multisample FBO. The scene rasterises here with MSAA_SAMPLES
    // samples per pixel; we never sample it in a shader, only resolve it by blit
    // into the intermediate FBO below.
    unsigned int fbo, fboColor, fboDepth;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    // Color: multisample texture. Could also be a multisample renderbuffer; texture
    // is here purely to match the LearnOpenGL example structure.
    glGenTextures(1, &fboColor);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, fboColor);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, MSAA_SAMPLES, GL_RGB, RENDER_WIDTH, RENDER_HEIGHT, GL_TRUE);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, fboColor, 0);

    // Depth: matching multisample renderbuffer. Must match color's sample count or
    // the framebuffer is incomplete.
    glGenRenderbuffers(1, &fboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, fboDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, MSAA_SAMPLES, GL_DEPTH_COMPONENT24, RENDER_WIDTH, RENDER_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR: MSAA framebuffer is incomplete\n";

    // Pass 1.5 target: single-sample FBO that receives the resolved MSAA color via
    // glBlitFramebuffer. Its color texture is then sampled by the screen quad in
    // pass 2.
    unsigned int intermediateFbo, screenTexture;
    glGenFramebuffers(1, &intermediateFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, intermediateFbo);
    glGenTextures(1, &screenTexture);
    glBindTexture(GL_TEXTURE_2D, screenTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, RENDER_WIDTH, RENDER_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR: Intermediate framebuffer is incomplete\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Fullscreen quad in NDC space. With glViewport set to the letterbox rect
    // before drawing, this maps to exactly that rect on the window.
    const float quadVertices[] = {
        // pos        uv
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    unsigned int quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);

    //mouse capture
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    //glfwSetCursorPosCallback(window, mouse_callback);
    if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

    InputManager input(window);
    input.BindAction("Quit", GLFW_KEY_ESCAPE);
    input.BindAction("CameraMoveForward", GLFW_KEY_W);
    input.BindAction("CameraMoveBackward", GLFW_KEY_S);
    input.BindAction("CameraMoveLeft", GLFW_KEY_A);
    input.BindAction("CameraMoveRight", GLFW_KEY_D);
    input.BindAction("CameraMoveUp", GLFW_KEY_Q);
    input.BindAction("CameraMoveDown", GLFW_KEY_E);
    input.BindAction("MoveBackward", GLFW_KEY_DOWN);
    input.BindAction("MoveForward", GLFW_KEY_UP);
    input.BindAction("MoveRight", GLFW_KEY_RIGHT);
    input.BindAction("MoveLeft", GLFW_KEY_LEFT);
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
    player->addComponent<TransformComponent>()->Position = {2.0f, -1.0f, 0};
    //player->addComponent<MeshComponent>("assets/models/nier/2B.obj");
    player->addComponent<MeshComponent>("assets/models/backpack/backpack.obj", /*flipUVs=*/true); 
    //flip UVs. Different models have different conventions, flip them if troubleshooting.
    player->addComponent<InputComponent>(&input);
    player->addComponent<ColliderComponent3D>();
    player->addComponent<CharacterComponent>();
    player->addComponent<CharacterController>();
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
    camera->addComponent<InputComponent>(&input);
    camera->addComponent<CameraComponent>();
    camera->addComponent<CameraController>();

    //camera->addComponent<CameraFollow>(); //script
    //camera->getComponent<CameraFollow>()->Target = player.get();
    camera->start();



    Shader shaderProgram("assets/shaders/vertex_s.vs", "assets/shaders/fragment_s.fs");
    Shader shaderLight("assets/shaders/light_s.vs", "assets/shaders/light_s.fs");
    Shader shaderLighting("assets/shaders/advanced_lighting.vs", "assets/shaders/advanced_lighting.fs");
    Shader msaaPost("assets/shaders/msaa_post.vs", "assets/shaders/msaa_post.fs");
    msaaPost.use();
    msaaPost.setInt("screenTexture", 0);

    //TODO: change path loading system


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
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto* cam = camera->getComponent<CameraComponent>(); //get camera component might be null
        glm::mat4 view = cam->viewMatrix();
        // Aspect ratio is fixed by the internal resolution now, NOT the window.
        glm::mat4 proj = cam->projectionMatrix((float)RENDER_WIDTH / (float)RENDER_HEIGHT);
        const glm::vec3 camPos = cam->owner->getComponent<TransformComponent>()->Position;

        // Update all transforms before drawing so the light source's position
        // (read off the fish below) is current for this frame.
        player->update(deltaTime);
        fish->update(deltaTime);
        ColliderComponent3D::tickWorld();

        // Light source: the spinning cube. Drawn first with the unlit shader so
        // we can sample its transform for lightPos before lighting the player.
        const glm::vec3 lightPos = fish->getComponent<TransformComponent>()->Position;

        shaderLight.use();
        shaderLight.setMat4("view", view);
        shaderLight.setMat4("projection", proj);
        fish->getComponent<MeshComponent>()->draw(shaderLight);

        // Lit object: the player. advanced_lighting samples texture_diffuse1 for
        // base color (bound automatically by Mesh::Draw) and branches on `blinn`
        // between Phong and Blinn-Phong specular.
        shaderLighting.use();
        shaderLighting.setMat4("view", view);
        shaderLighting.setMat4("projection", proj);
        shaderLighting.setVec3("lightPos", lightPos.x, lightPos.y, lightPos.z);
        shaderLighting.setVec3("viewPos",  camPos.x,   camPos.y,   camPos.z);
        shaderLighting.setBool("blinn", true);
        player->getComponent<MeshComponent>()->draw(shaderLighting);

        //playground->update(deltaTime);
        //shaderProgram.use();
        //shaderProgram.setMat4("view", view);
        //shaderProgram.setMat4("projection", proj);
        //playground->getComponent<MeshComponent>()->draw(shaderProgram);

        // Pass 1.5: resolve MSAA. Source and dest rects must match in size when the
        // source has samples > 0, so we resolve at native render resolution into
        // the single-sample intermediate FBO.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, intermediateFbo);
        glBlitFramebuffer(0, 0, RENDER_WIDTH, RENDER_HEIGHT,
                          0, 0, RENDER_WIDTH, RENDER_HEIGHT,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);

        // Pass 2: draw a fullscreen quad sampling the resolved texture into the
        // letterbox/pillarbox region of the window. The viewport handles scaling.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const float renderAspect = (float)RENDER_WIDTH / (float)RENDER_HEIGHT;
        const float windowAspect = (float)SCR_WIDTH    / (float)SCR_HEIGHT;
        int dx, dy, dw, dh;
        if (windowAspect > renderAspect) {
            dh = SCR_HEIGHT;
            dw = (int)(SCR_HEIGHT * renderAspect);
            dx = ((int)SCR_WIDTH - dw) / 2;
            dy = 0;
        } else {
            dw = SCR_WIDTH;
            dh = (int)(SCR_WIDTH / renderAspect);
            dx = 0;
            dy = ((int)SCR_HEIGHT - dh) / 2;
        }
        glViewport(dx, dy, dw, dh);
        msaaPost.use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, screenTexture);
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        input.ResetFrame();

        //check all events and swap buffer
        glfwSwapBuffers(window);
        glfwPollEvents();    //SO events like key, mouse, etc
    }

    // cleanup
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &fboColor);
    glDeleteRenderbuffers(1, &fboDepth);
    glDeleteFramebuffers(1, &intermediateFbo);
    glDeleteTextures(1, &screenTexture);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteBuffers(1, &quadVBO);

    // Release GPU-owning objects while the GL context is still alive.
    // Mesh/Model dtors call glDelete*; running them after glfwTerminate() segfaults.
    playground.reset();
    player.reset();
    fish.reset();
    camera.reset();

    glfwTerminate();
    return 0;
}