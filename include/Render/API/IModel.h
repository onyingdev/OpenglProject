#pragma once
#include <glm/glm.hpp>

class IShader;

// Which transparency pass a draw belongs to. A model splits its meshes by glTF alphaMode: OPAQUE/MASK
// meshes draw in the Opaque pass, BLEND meshes in the (sorted/blended) Transparent pass.
enum class MaterialPass { Opaque, Transparent };

// Backend-agnostic model: a collection of meshes drawn together under one shader.
// Loading (Assimp, etc.) and GPU upload live in the concrete backend; the engine
// holds models only as IModel and draws them through this interface.
class IModel {
public:
    virtual ~IModel() = default;
    virtual void Draw(IShader& shader) = 0;

    // Draw `count` copies in ONE instanced call, each transformed by its own matrix in `instances`.
    // The backend owns how the per-instance matrices reach the shader (a GL instance buffer here);
    // the shader must read the model matrix from a per-instance attribute, not a uniform. `pass`
    // selects which alpha class of meshes to draw (Opaque skips BLEND meshes, and vice versa).
    virtual void DrawInstanced(IShader& shader, const glm::mat4* instances, int count,
                               MaterialPass pass = MaterialPass::Opaque) = 0;
};
