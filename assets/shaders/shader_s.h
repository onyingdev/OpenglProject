#ifndef SHADER_H
#define SHADER_H

#include "../../vendor/glad/glad/glad.h"
#include "../../include/Core/glm/glm/glm.hpp"
#include "../../include/Core/glm/glm/gtc/matrix_transform.hpp"
#include "../../include/Core/glm/glm/gtc/type_ptr.hpp"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream> 

class Shader 
{
    public:
    //program ID
    unsigned int ID;

    //constructor reads and builds the shader
    Shader(const char* vertexPath, const char* fragmentPath);
   
    void use(); //use/activate the shader
   //utility uniform functions
    void setBool(const std::string &name, bool value) const;
    void setInt(const std::string &name, int value) const;
    void setFloat(const std::string &name, float value) const;
    void setVec4(const std::string &name, float x, float y, float z, float w) const;
    void setMat4(const std::string &name, unsigned int number_matrices, unsigned char GLboolean, glm::mat4 trans) const;
};

#endif

