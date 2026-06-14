#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Render/API/IShader.h"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

// OpenGL implementation of IShader. Owns a GL program object (ID).
class Shader : public IShader
{
    public:
    //program ID
    unsigned int ID;

    //constructor reads and builds the shader
    Shader(const char* vertexPath, const char* fragmentPath);

    void use() override; //use/activate the shader
   //utility uniform functions
    void setBool(const std::string &name, bool value) const override;
    void setInt(const std::string &name, int value) const override;
    void setFloat(const std::string &name, float value) const override;
    void setVec2(const std::string &name, float x, float y) const override;
    void setVec3(const std::string &name, float x, float y, float z) const override;
    void setVec4(const std::string &name, float x, float y, float z, float w) const override;
    void setMat2(const std::string &name, const glm::mat2 &mat) const override;
    void setMat3(const std::string &name, const glm::mat3 &mat) const override;
    void setMat4(const std::string &name, const glm::mat4 &mat) const override;

    private:
    void checkCompileErrors(GLuint shader, std::string type);

    // Uniform-location cache. glGetUniformLocation is a slow string→driver query; calling it on every
    // setX (per object, per frame) was a real cost at high object counts. Look each name up once and
    // reuse. Cached even when -1 (not found): a glUniform with location -1 is a silent no-op, matching
    // the previous behavior. mutable because the setX accessors are const.
    mutable std::unordered_map<std::string, int> m_locCache;
    int loc(const std::string& name) const;
};

#endif

