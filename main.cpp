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
#include "include/Core/component.h"
#include "include/Core/gameObject.h"
#include "include/Components/transformComponent.h"
#include "include/Components/inputComponent.h"
#include "include/Components/cameraComponent.h"
#include "assets/scripts/cameraController.h"

//TODO: refactor with main to decouple functions

using namespace std;

//global attributes
unsigned int SCR_WIDTH = 1920;
unsigned int SCR_HEIGHT = 1080;

//time
float deltaTime = 0.0f;	// Time between current frame and last frame
float lastFrame = 0.0f; // Time of last frame

//resize, its called everytime we resize the window
//to call it we need to tell glfw
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
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
    // //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); //MAC OS
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    //create window
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
    
    //viewport for opengl to render, 
    //this is the dimensions opengl will render not the window size
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

    //resize
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    //mouse capture
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    //glfwSetCursorPosCallback(window, mouse_callback);
    if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

    InputManager input(window);
    input.BindAction("Quit", GLFW_KEY_ESCAPE);
    input.BindAction("MoveForward", GLFW_KEY_W);
    input.BindAction("MoveBackward", GLFW_KEY_S);
    input.BindAction("MoveLeft", GLFW_KEY_A);
    input.BindAction("MoveRight", GLFW_KEY_D);

    auto camera = std::make_unique<GameObject>();
    camera->Name = "MainCamera";
    camera->addComponent<TransformComponent>()->Position = {0, 0, 3};
    //camera->addComponent<InputComponent>(&input);
    camera->addComponent<CameraComponent>();
    camera->addComponent<CameraController>(); //script
    camera->start();

    Shader shaderProgram("assets/shaders/vertex_s.vs", "assets/shaders/fragment_s.fs");
    Model ourModel("assets/models/backpack/backpack.obj"); //TODO: change path loading system


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

        //rendering commands
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f); // cleaning screen
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shaderProgram.use();

        auto* cam = camera->getComponent<CameraComponent>(); //get camera component might be null
        glm::mat4 view = cam->viewMatrix();
        glm::mat4 proj = cam->projectionMatrix((float)SCR_WIDTH / (float)SCR_HEIGHT);
        shaderProgram.setMat4("view", view);
        shaderProgram.setMat4("projection", proj);


        // render the loaded model
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.0f)); // translate it down so it's at the center of the scene
        model = glm::scale(model, glm::vec3(1.0f, 1.0f, 1.0f));	// it's a bit too big for our scene, so scale it down
        shaderProgram.setMat4("model", model);
        ourModel.Draw(shaderProgram);

        input.ResetFrame();

        //check all events and swap buffer
        glfwSwapBuffers(window);
        glfwPollEvents();    //SO events like key, mouse, etc
    }

    glfwTerminate();
    return 0;
}