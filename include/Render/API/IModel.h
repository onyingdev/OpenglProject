#pragma once

class IShader;

// Backend-agnostic model: a collection of meshes drawn together under one shader.
// Loading (Assimp, etc.) and GPU upload live in the concrete backend; the engine
// holds models only as IModel and draws them through this interface.
class IModel {
public:
    virtual ~IModel() = default;
    virtual void Draw(IShader& shader) = 0;
};
