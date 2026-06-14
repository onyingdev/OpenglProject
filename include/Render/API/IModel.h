#pragma once
#include <glm/glm.hpp>

class IShader;

// Backend-agnostic model: a collection of meshes drawn together under one shader.
// Loading (Assimp, etc.) and GPU upload live in the concrete backend; the engine
// holds models only as IModel and draws them through this interface.
class IModel {
public:
    virtual ~IModel() = default;
    virtual void Draw(IShader& shader) = 0;

    // Draw `count` copies in ONE instanced call, each transformed by its own matrix in `instances`.
    // The backend owns how the per-instance matrices reach the shader (a GL instance buffer here);
    // the shader must read the model matrix from a per-instance attribute, not a uniform.
    virtual void DrawInstanced(IShader& shader, const glm::mat4* instances, int count) = 0;
};
