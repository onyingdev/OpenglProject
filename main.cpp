#include <iostream>
#include <cmath>
#include "vendor/glad/glad/glad.h" //glad goes first
#include "vendor/GLFW/include/glfw3.h"
#include "assets/shaders/shader_s.h"
#include "assets/scripts/camera.h"

//#define STB_IMAGE_IMPLEMENTATION
//#include "assets/textures/stb_image.h"
//#include "include/Core/lib_asset_loader.h"
#include "include/Render/model.h"
#include "include/Core/input_manager.h"

//TODO: refactor with main to decouple functions

using namespace std;

//global attributes
unsigned int SCR_WIDTH = 800;
unsigned int SCR_HEIGHT = 600;

// camera
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
//float lastX = SCR_WIDTH / 2.0f;
//float lastY = SCR_HEIGHT / 2.0f;
//bool firstMouse = true;

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

//void mouse_callback(GLFWwindow* window, double xpos, double ypos)
//{
//    if (firstMouse)
//    {
//        lastX = xpos;
//        lastY = ypos;
//        firstMouse = false;
//    }
//
//    float xoffset = xpos - lastX;
//    float yoffset = lastY - ypos;
//    lastX = xpos;
//    lastY = ypos;
//
//    camera.ProcessMouseMovement(xoffset, yoffset);
//}

////input function
////glfwGetKey, what window and what is pressed
//void processInput(GLFWwindow *window)
//{
//    const float cameraSpeed = 2.5f * deltaTime;
//
//    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
//        glfwSetWindowShouldClose(window, true);
//    if(glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
//        contrast += 0.01f;
//    }
//    if(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
//        contrast -= 0.01f;
//    }
//    if(glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
//        rotation += 1.0f;
//    }
//    if(glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
//        rotation -= 1.0f;
//    }
//
//    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
//        glfwSetWindowShouldClose(window, true);
//
//    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
//        camera.ProcessKeyboard(FORWARD, deltaTime);
//    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
//        camera.ProcessKeyboard(BACKWARD, deltaTime);
//    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
//        camera.ProcessKeyboard(LEFT, deltaTime);
//    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
//        camera.ProcessKeyboard(RIGHT, deltaTime);
//}



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
    GLFWwindow* window = glfwCreateWindow(800, 600, "OpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
  
    //initialize GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    stbi_set_flip_vertically_on_load(true);
    
    //viewport for opengl to render, 
    //this is the dimensions opengl will render not the window size
    glViewport(0, 0, 800, 600);

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

    Shader shaderProgram("assets/shaders/vertex_s.vs", "assets/shaders/fragment_s.fs");
    Model ourModel("/home/vorasak/blender/backpack/backpack.obj");


    //main loop the more you can take out of the loop the better
    while(!glfwWindowShouldClose(window))
    {
        //input
        //processInput(window);

        //calculate physics
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (input.IsActionReleased("Quit")) {
            glfwSetWindowShouldClose(window, true);
        }
        if (input.IsActionPressed("MoveForward")) {
            camera.ProcessKeyboard(FORWARD, deltaTime);
        }
        if (input.IsActionPressed("MoveBackward")) {
            camera.ProcessKeyboard(BACKWARD, deltaTime);
        }
        if (input.IsActionPressed("MoveLeft")) {
            camera.ProcessKeyboard(LEFT, deltaTime);
        }
        if (input.IsActionPressed("MoveRight")) {
            camera.ProcessKeyboard(RIGHT, deltaTime);
        }

        // Process Mouse Look
        camera.ProcessMouseMovement(input.GetMouseOffsetX(), input.GetMouseOffsetY());

        //rendering commands
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f); // cleaning screen
        glEnable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shaderProgram.use();

        glm::mat4 view = camera.GetViewMatrix();
        shaderProgram.setMat4("view", view);
        // Automatically adjust the aspect ratio so objects never stretch
        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
        shaderProgram.setMat4("projection", projection);

//        shaderProgram.setFloat("contrast", contrast);

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