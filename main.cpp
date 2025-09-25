#include <iostream>
#include "glad/glad/glad.h" //glado goes first
#include "GLFW/include/glfw3.h"
using namespace std;

const char *vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
    "}\0";

const char *fragmentShaderSource = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = vec4(1.0f, 1.0f, 0.0f, 1.0f);\n"
    "}\0";

//resize, its called everytime we resize the window
//to call it we need to tell glfw
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
} 

unsigned int createShaderProgram (const char *vertexShaderS, const char *fragmentShaderS) {
    //create vertex shader
    unsigned int vertexShader;
    vertexShader = glCreateShader(GL_VERTEX_SHADER);    //create shader
    glShaderSource(vertexShader, 1, &vertexShaderS, NULL); //define source
    glCompileShader(vertexShader);  //compile
    int  success;   //error check
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    //create fragment shader
    unsigned int fragmentShader;
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderS, NULL); 
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    //create shader program
    unsigned int shaderProgram;
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader); //attach shader and link them
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success); //error check
    if(!success)
    {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    glUseProgram(shaderProgram); //activate shader program
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);  
    //once linked we can delete and keep the program
    //note we can create many shaders, link them and use them whenever we want

    return shaderProgram;
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

    unsigned int shaderProgram = createShaderProgram(vertexShaderSource, fragmentShaderSource);

    //create triangle
    // float vertices[] = {
    //     -0.5f, -0.5f, 0.0f, //bottom left
    //     0.5f, -0.5f, 0.0f, //top left
    //     -0.5f,  0.5f, 0.0f, //bottom right
    //     0.5f, 0.5f, 0.0f //top right
    // };

    // unsigned int indices[]={ //start from 0
    //     0, 1, 2,    //first triangle
    //     1, 2, 3     //second triangle
    // };

    float vertices[] = {
        -2.0f, -0.5f, 0.0f,
        -0.5f, 0.5f, 0.0f,
        0.0f, -0.5f, 0.0f,

        0.0f, 0.5f, 0.0f,
        1.0f, 0.5f, 0.0f,
        0.5f, -0.5f, 0.0f
    };


    //drawing triangle
    //create VBO
    unsigned int VBO;
    glGenBuffers(1, &VBO); 
    //create VAO
    unsigned int VAO;
    glGenVertexArrays(1, &VAO); 
    //create EBO
    unsigned int EBO;
    glGenBuffers(1, &EBO);

    // bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
    glBindVertexArray(VAO);
    //bind vbo
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // note that this is allowed, the call to glVertexAttribPointer registered VBO as the vertex attribute's bound vertex buffer object so afterwards we can safely unbind
    glBindBuffer(GL_ARRAY_BUFFER, 0); 

    // You can unbind the VAO afterwards so other VAO calls won't accidentally modify this VAO, but this rarely happens. Modifying other
    // VAOs requires a call to glBindVertexArray anyways so we generally don't unbind VAOs (nor VBOs) when it's not directly necessary.
    glBindVertexArray(0); 

    // //Bind EBO
    // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    // glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

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



    //main loop
    while(!glfwWindowShouldClose(window))
    {
        //input
        processInput(window);

        //rendering commands
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f); // cleaning screen
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram); //rendering triangle
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        //glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0); //draw using index, bind buffer before using it
        //check all events and swap buffer
        glfwSwapBuffers(window);
        glfwPollEvents();    //SO events like key, mouse, etc
    }

    //ALWAYS CLEAN
    glfwTerminate();
    return 0;
}