#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Backend-agnostic, CPU-side asset data — the intermediate between the loader and any GPU backend.
//
// The loader (Model::parse) fills these from disk (Assimp + stb_image) touching NO graphics API, so it
// can run on a worker thread. A backend then uploads a ModelData into its own GPU resources on the
// render thread (OpenGL) or a transfer queue (future Vulkan). Splitting "what the asset is" (here) from
// "how it reaches the GPU" (backend) is what lets the heavy parse/decode move off the main thread.
//
// No GL headers, no `using namespace`: this header is included by both the loader and the renderer.

#ifndef MAX_BONE_INFLUENCE
#define MAX_BONE_INFLUENCE 4
#endif

// One vertex. Lives here (not mesh.h) so the loader and the GPU upload share a single definition.
struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    int   m_BoneIDs[MAX_BONE_INFLUENCE];   // skeletal animation (unused until skinning lands)
    float m_Weights[MAX_BONE_INFLUENCE];
};

// A decoded texture before any GPU upload: raw pixels + dimensions. `pixels` empty => decode failed,
// and the backend should substitute its missing-texture fallback. `type` is the sampler-name family
// ("texture_diffuse"/"texture_specular"/...); `path` keys de-duplication within a model.
struct TextureData {
    std::vector<unsigned char> pixels;
    int width    = 0;
    int height   = 0;
    int channels = 0;
    std::string type;
    std::string path;
};

// One mesh: geometry + indices into the owning ModelData's shared texture pool.
struct MeshData {
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;
    std::vector<int>          textureIndices;   // -> ModelData::textures
    glm::vec3                 baseColor{1.0f};   // material color factor; multiplied with the diffuse map
                                                 // (white = no-op for textured meshes)
};

// A whole model as parsed from disk: every mesh, plus a de-duplicated texture pool the meshes index
// into. Consumed (moved) by the backend's upload step, after which it can be discarded.
struct ModelData {
    std::vector<MeshData>    meshes;
    std::vector<TextureData> textures;
    std::string directory;
    bool gammaCorrection = false;
    bool flipUVs         = false;
    bool valid           = false;   // false if Assimp failed to read the file
};
