#include <iostream>
#include <cmath>
#include "vendor/glad/glad/glad.h" //glad goes first
#include "vendor/GLFW/include/glfw3.h"
#include "assets/shaders/shader_s.h"

#define STB_IMAGE_IMPLEMENTATION
#include "assets/textures/stb_image.h"

//TODO: refactor with main to decouple functions

using namespace std;

//resize, its called everytime we resize the window
//to call it we need to tell glfw
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
} 

//input function
//glfwGetKey, what window and what is pressed
void processInput(GLFWwindow *window)
{
    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
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
    GLFWwindow* window = glfwCreateWindow(800, 600, "LearnOpenGL", NULL, NULL);
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
    
    //viewport for opengl to render, 
    //this is the dimensions opengl will render not the window size
    glViewport(0, 0, 800, 600);

    //resize
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    Shader shaderProgram("assets/shaders/vertex_s.vs", "assets/shaders/fragment_s.fs");

   float vertices[] = {
        // positions          // colors           // texture coords
        0.5f,  0.5f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f,   // top right
        0.5f, -0.5f, 0.0f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f,   // bottom right
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f,   // bottom left
        -0.5f,  0.5f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f    // top left 
    };
    unsigned int indices[] = {  
        0, 1, 3, // first triangle
        1, 2, 3  // second triangle
    };

    //drawing shape
    //create VBO, VAO and EBO
    unsigned int VBO, VAO, EBO;
    glGenBuffers(1, &VBO); 
    glGenVertexArrays(1, &VAO); 
    glGenBuffers(1, &EBO);

    // bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
    glBindVertexArray(VAO);
    //bind vbo
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    //Bind EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    //position attribute
    glVertexAttribPointer(
        0,                  // index of the vertex attribute
        3,                  // number of components per attribute
        GL_FLOAT,           // type of each component
        GL_FALSE,           // should the data be normalized?
        8 * sizeof(float),  // stride (distance in bytes between each attribute)
        (void*)0            // offset (where the first attribute starts in the buffer)
    );
    glEnableVertexAttribArray(0); //enable the vertex attribute, its the layout location, can have many attributes as hardware allows

    //color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3* sizeof(float)));
    glEnableVertexAttribArray(1);

    //texture attribute
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(2);  

    // note that this is allowed, the call to glVertexAttribPointer registered VBO as the vertex attribute's bound vertex buffer object so afterwards we can safely unbind
    glBindBuffer(GL_ARRAY_BUFFER, 0); 

    // You can unbind the VAO afterwards so other VAO calls won't accidentally modify this VAO, but this rarely happens. Modifying other
    // VAOs requires a call to glBindVertexArray anyways so we generally don't unbind VAOs (nor VBOs) when it's not directly necessary.
    glBindVertexArray(0); 



    /*
        step 1
            create VAO, VBO, EBO
        step 2
            1. bind VAO, 2. then bind buffer with VBO and especify buffer data with vertices
        step 3
            configure vertex attributes
        step 4
            enable vertex attribute array
        step 5
            bind buffer 0 (unbind)
        step 6
            bind vertex array 0 (unbind)
    */

    //create texture param
    unsigned int texture1, texture2;
    glGenTextures(1, &texture1);
    glBindTexture(GL_TEXTURE_2D, texture1); 
    //define texture wrapping, filtering and mipmaps
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    //load and create a texture
    int width, height, nrChannels;
    unsigned char *data = stbi_load("assets/textures/container.jpg", &width, &height, &nrChannels, 0);

    if (data) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else {
        std::cout << "Failed to load texture" << std::endl;
    }
    //free image memory IMPORTANT!
    stbi_image_free(data);

    //texture2
    glGenTextures(1, &texture2);
    glBindTexture(GL_TEXTURE_2D, texture2); 
    //define texture wrapping, filtering and mipmaps
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_set_flip_vertically_on_load(true); 
    data = stbi_load("assets/textures/awesomeface.png", &width, &height, &nrChannels, 0);
    
    if (data) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else {
        std::cout << "Failed to load texture" << std::endl;
    }
    //free image memory IMPORTANT!
    stbi_image_free(data);
    
    shaderProgram.use();
    glUniform1i(glGetUniformLocation(shaderProgram.ID, "texture1"), 0);
    shaderProgram.setInt("texture2", 1);

    float contrast = 0.2f;

    //main loop the more you can take out of the loop the better
    while(!glfwWindowShouldClose(window))
    {
        //input
        processInput(window);

        if(glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            contrast += 0.01f;
        }
        if(glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            contrast -= 0.01f;
        }

        //rendering commands
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f); // cleaning screen
        glClear(GL_COLOR_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture2);
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        shaderProgram.setFloat("contrast", contrast);
      
        //check all events and swap buffer
        glfwSwapBuffers(window);
        glfwPollEvents();    //SO events like key, mouse, etc
    }

    //ALWAYS CLEAN
    glfwTerminate();
    return 0;
}