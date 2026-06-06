#pragma once

class IShader;

// Backend-agnostic renderable mesh: knows how to issue its own draw call given an
// already-bound shader. The OpenGL implementation owns the VAO/VBO/EBO; other
// backends own their equivalent handles. The engine never sees the GPU resources.
class IMesh {
public:
    virtual ~IMesh() = default;
    virtual void Draw(IShader& shader) = 0;
};
