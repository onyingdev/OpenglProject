#ifndef SHADER_H
#define SHADER_H

#include "../glad/glad/glad.h"
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
};

#endif

