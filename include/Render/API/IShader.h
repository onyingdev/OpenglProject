#pragma once
#include <string>
#include <glm/glm.hpp>

// Backend-agnostic shader program interface. The engine binds the program and sets
// uniforms through this surface; a concrete backend (OpenGL today, Vulkan/other
// later) provides the implementation. Mirrors the uniform setters the renderer and
// meshes rely on, so swapping backends never touches engine-side draw code.
class IShader {
public:
    virtual ~IShader() = default;

    virtual void use() = 0;  // activate the program for subsequent draws

    virtual void setBool (const std::string& name, bool value) const = 0;
    virtual void setInt  (const std::string& name, int value) const = 0;
    virtual void setFloat(const std::string& name, float value) const = 0;
    virtual void setVec2 (const std::string& name, float x, float y) const = 0;
    virtual void setVec3 (const std::string& name, float x, float y, float z) const = 0;
    virtual void setVec4 (const std::string& name, float x, float y, float z, float w) const = 0;
    virtual void setMat2 (const std::string& name, const glm::mat2& mat) const = 0;
    virtual void setMat3 (const std::string& name, const glm::mat3& mat) const = 0;
    virtual void setMat4 (const std::string& name, const glm::mat4& mat) const = 0;
};
