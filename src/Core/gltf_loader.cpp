// glTF 2.0 loader — tinygltf-backed implementation of gltf::Load (declared in Render/Assets/GLTFLoader.h).
//
// This translation unit is deliberately the *only* place tinygltf's implementation lives, to keep its
// dependencies from clashing with the rest of the engine:
//
//   * stb_image: the implementation is already compiled once in src/Core/lib_asset_loader.cpp. We tell
//     tinygltf NOT to include/define its own (TINYGLTF_NO_INCLUDE_STB_IMAGE) and instead include the
//     project's stb_image.h for *declarations only* — the stbi_* symbols resolve to that single shared
//     implementation at link time. No duplicate-symbol errors.
//   * stb_image_write: unused (we never write images) — fully disabled.
//   * json: tinygltf includes its own bundled json.hpp (nlohmann 3.10.4). The rest of the engine uses
//     <nlohmann/json.hpp> 3.11.3. They never meet because this TU includes neither the engine's json nor
//     any header that pulls it; the versions also live in different inline namespaces, so even the linker
//     keeps them apart.

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE        // use the engine's stb_image (declarations) ...
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#define STBI_MSC_SECURE_CRT                  // harmless on non-MSVC; silences a tinygltf write guard

#include "Core/Engine/Helpers/stb_image.h"  // ... included here WITHOUT STB_IMAGE_IMPLEMENTATION
#include "tiny_gltf.h"

#include "Render/Assets/GLTFLoader.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>

namespace gltf {

namespace {

// Lowercase file extension (no dot), e.g. "scene.GLB" -> "glb".
std::string extOf(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    return e;
}

// Number of components for a glTF accessor type (VEC3 -> 3, SCALAR -> 1, ...).
int compCount(int type) {
    switch (type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2:   return 2;
        case TINYGLTF_TYPE_VEC3:   return 3;
        case TINYGLTF_TYPE_VEC4:   return 4;
        default:                   return 0;
    }
}

// Read one scalar element of an accessor at index `i`, component `c`, normalized to float. Handles the
// component types glTF allows for vertex attributes (float + normalized/unnormalized integer types),
// which is enough for POSITION/NORMAL/TANGENT/TEXCOORD.
float readComponent(const tinygltf::Model& m, const tinygltf::Accessor& acc, size_t i, int c) {
    const tinygltf::BufferView& view = m.bufferViews[acc.bufferView];
    const tinygltf::Buffer& buf      = m.buffers[view.buffer];
    const int n        = compCount(acc.type);
    const int compSize = tinygltf::GetComponentSizeInBytes(acc.componentType);
    const size_t stride = acc.ByteStride(view) ? (size_t)acc.ByteStride(view) : (size_t)n * compSize;
    const unsigned char* base = buf.data.data() + view.byteOffset + acc.byteOffset + i * stride + (size_t)c * compSize;

    switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            float v; std::memcpy(&v, base, sizeof v); return v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            unsigned char v = *base; return acc.normalized ? v / 255.0f : (float)v;
        }
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            signed char v = (signed char)*base; return acc.normalized ? std::max(v / 127.0f, -1.0f) : (float)v;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            unsigned short v; std::memcpy(&v, base, sizeof v); return acc.normalized ? v / 65535.0f : (float)v;
        }
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            short v; std::memcpy(&v, base, sizeof v); return acc.normalized ? std::max(v / 32767.0f, -1.0f) : (float)v;
        }
        default:
            return 0.0f;
    }
}

// Read an index from the (optional) index accessor.
unsigned int readIndex(const tinygltf::Model& m, const tinygltf::Accessor& acc, size_t i) {
    const tinygltf::BufferView& view = m.bufferViews[acc.bufferView];
    const tinygltf::Buffer& buf      = m.buffers[view.buffer];
    const size_t stride = acc.ByteStride(view) ? (size_t)acc.ByteStride(view)
                                               : (size_t)tinygltf::GetComponentSizeInBytes(acc.componentType);
    const unsigned char* base = buf.data.data() + view.byteOffset + acc.byteOffset + i * stride;
    switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  return *base;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { unsigned short v; std::memcpy(&v, base, sizeof v); return v; }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   { unsigned int   v; std::memcpy(&v, base, sizeof v); return v; }
        default: return 0;
    }
}

// Local transform of a node: an explicit `matrix` if present, otherwise composed from TRS.
glm::mat4 localTransform(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        double m[16];
        for (int i = 0; i < 16; ++i) m[i] = node.matrix[i];
        return glm::make_mat4(m);   // glTF matrices are column-major, same as glm
    }
    glm::mat4 T(1.0f), R(1.0f), S(1.0f);
    if (node.translation.size() == 3)
        T = glm::translate(glm::mat4(1.0f),
                           glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    if (node.rotation.size() == 4)   // glTF quat is (x, y, z, w); glm::quat ctor is (w, x, y, z)
        R = glm::mat4_cast(glm::quat((float)node.rotation[3], (float)node.rotation[0],
                                     (float)node.rotation[1], (float)node.rotation[2]));
    if (node.scale.size() == 3)
        S = glm::scale(glm::mat4(1.0f), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    return T * R * S;
}

// Resolve a glTF texture's image into the model's de-duplicated texture pool, tagging it `typeName`
// ("texture_diffuse"/"texture_normal"/...). Returns its pool index, or -1 if the texture/image is
// missing or undecoded. De-dupes on (image source, type) so a shared image isn't copied twice.
int addTexture(const tinygltf::Model& gm, int texIndex, const char* typeName, ModelData& out,
               std::map<std::pair<int, std::string>, int>& seen) {
    if (texIndex < 0 || texIndex >= (int)gm.textures.size()) return -1;
    const int src = gm.textures[texIndex].source;
    if (src < 0 || src >= (int)gm.images.size()) return -1;

    const auto key = std::make_pair(src, std::string(typeName));
    if (auto it = seen.find(key); it != seen.end()) return it->second;

    const tinygltf::Image& img = gm.images[src];
    TextureData td;
    td.type = typeName;
    td.path = img.uri.empty() ? (img.name.empty() ? std::to_string(src) : img.name) : img.uri;
    if (img.width > 0 && img.height > 0 && img.component > 0 && !img.image.empty() && img.bits == 8) {
        td.width = img.width; td.height = img.height; td.channels = img.component;
        td.pixels = img.image;   // tinygltf already decoded to 8-bit pixels (RAM trade for thread-safety)
    } else {
        std::cout << "glTF: image '" << td.path << "' undecoded/unsupported (bits=" << img.bits
                  << ") — using fallback\n";   // empty pixels -> upload substitutes the magenta fallback
    }
    out.textures.push_back(std::move(td));
    const int idx = (int)out.textures.size() - 1;
    seen.emplace(key, idx);
    return idx;
}

// Convert one primitive into a MeshData, baking `world` into positions (and its normal matrix into
// normals/tangents). Skips non-triangle primitives — the engine draws GL_TRIANGLES only.
void processPrimitive(const tinygltf::Model& gm, const tinygltf::Primitive& prim, const glm::mat4& world,
                      bool flipUVs, ModelData& out, std::map<std::pair<int, std::string>, int>& seen) {
    if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) return;

    auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) return;   // nothing to draw without positions

    const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(world)));

    MeshData md;
    const tinygltf::Accessor& posAcc = gm.accessors[posIt->second];
    const size_t vertexCount = posAcc.count;
    md.vertices.resize(vertexCount);   // value-inits bone ids/weights + unused attrs to zero

    for (size_t i = 0; i < vertexCount; ++i) {
        glm::vec4 p(readComponent(gm, posAcc, i, 0), readComponent(gm, posAcc, i, 1),
                    readComponent(gm, posAcc, i, 2), 1.0f);
        md.vertices[i].Position = glm::vec3(world * p);
    }

    if (auto it = prim.attributes.find("NORMAL"); it != prim.attributes.end()) {
        const tinygltf::Accessor& acc = gm.accessors[it->second];
        for (size_t i = 0; i < vertexCount; ++i)
            md.vertices[i].Normal = glm::normalize(normalMat * glm::vec3(
                readComponent(gm, acc, i, 0), readComponent(gm, acc, i, 1), readComponent(gm, acc, i, 2)));
    }

    if (auto it = prim.attributes.find("TEXCOORD_0"); it != prim.attributes.end()) {
        const tinygltf::Accessor& acc = gm.accessors[it->second];
        for (size_t i = 0; i < vertexCount; ++i) {
            float u = readComponent(gm, acc, i, 0);
            float v = readComponent(gm, acc, i, 1);
            md.vertices[i].TexCoords = glm::vec2(u, flipUVs ? 1.0f - v : v);
        }
    }

    if (auto it = prim.attributes.find("TANGENT"); it != prim.attributes.end()) {
        const tinygltf::Accessor& acc = gm.accessors[it->second];   // vec4: xyz tangent, w handedness
        for (size_t i = 0; i < vertexCount; ++i) {
            glm::vec3 t = glm::normalize(normalMat * glm::vec3(
                readComponent(gm, acc, i, 0), readComponent(gm, acc, i, 1), readComponent(gm, acc, i, 2)));
            md.vertices[i].Tangent   = t;
            md.vertices[i].Bitangent = glm::cross(md.vertices[i].Normal, t) * readComponent(gm, acc, i, 3);
        }
    }

    // Indices — glTF primitives are usually indexed; synthesize a 0..N-1 list if not.
    if (prim.indices >= 0) {
        const tinygltf::Accessor& idxAcc = gm.accessors[prim.indices];
        md.indices.reserve(idxAcc.count);
        for (size_t i = 0; i < idxAcc.count; ++i) md.indices.push_back(readIndex(gm, idxAcc, i));
    } else {
        md.indices.reserve(vertexCount);
        for (unsigned int i = 0; i < vertexCount; ++i) md.indices.push_back(i);
    }

    // Material textures -> pool indices. glTF PBR: base color -> diffuse, metallic-roughness -> specular
    // (approximate, fills the shader's specular slot), normal -> normal.
    if (prim.material >= 0 && prim.material < (int)gm.materials.size()) {
        const tinygltf::Material& mat = gm.materials[prim.material];
        // Base color factor — the flat material color, multiplied with the diffuse map at shade time.
        // Defaults to {1,1,1,1}, so a textured mesh with no explicit factor stays a no-op (white).
        const std::vector<double>& bcf = mat.pbrMetallicRoughness.baseColorFactor;
        md.baseColor = glm::vec3((float)bcf[0], (float)bcf[1], (float)bcf[2]);
        const int diffuse  = addTexture(gm, mat.pbrMetallicRoughness.baseColorTexture.index,
                                        "texture_diffuse", out, seen);
        const int specular = addTexture(gm, mat.pbrMetallicRoughness.metallicRoughnessTexture.index,
                                        "texture_specular", out, seen);
        const int normal   = addTexture(gm, mat.normalTexture.index, "texture_normal", out, seen);
        if (diffuse  >= 0) md.textureIndices.push_back(diffuse);
        if (specular >= 0) md.textureIndices.push_back(specular);
        if (normal   >= 0) md.textureIndices.push_back(normal);
    }

    out.meshes.push_back(std::move(md));
}

// Depth-first walk of the node hierarchy, accumulating world transforms and emitting a MeshData per
// primitive of each node's mesh.
void processNode(const tinygltf::Model& gm, int nodeIndex, const glm::mat4& parent, bool flipUVs,
                 ModelData& out, std::map<std::pair<int, std::string>, int>& seen) {
    const tinygltf::Node& node = gm.nodes[nodeIndex];
    const glm::mat4 world = parent * localTransform(node);

    if (node.mesh >= 0 && node.mesh < (int)gm.meshes.size())
        for (const tinygltf::Primitive& prim : gm.meshes[node.mesh].primitives)
            processPrimitive(gm, prim, world, flipUVs, out, seen);

    for (int child : node.children) processNode(gm, child, world, flipUVs, out, seen);
}

}  // namespace

bool isGLTF(const std::string& path) {
    const std::string e = extOf(path);
    return e == "gltf" || e == "glb";
}

ModelData Load(const std::string& path, bool gamma, bool flipUVs) {
    ModelData out;
    out.gammaCorrection = gamma;
    out.flipUVs         = flipUVs;
    out.directory       = path.substr(0, path.find_last_of('/'));

    tinygltf::Model gm;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    const bool ok = isGLTF(path) && extOf(path) == "glb"
                        ? loader.LoadBinaryFromFile(&gm, &err, &warn, path)
                        : loader.LoadASCIIFromFile(&gm, &err, &warn, path);
    if (!warn.empty()) std::cout << "glTF warning (" << path << "): " << warn << "\n";
    if (!ok || !err.empty()) {
        std::cout << "ERROR::TINYGLTF:: " << (err.empty() ? "failed to load " + path : err) << "\n";
        return out;   // out.valid stays false
    }

    std::map<std::pair<int, std::string>, int> seen;   // (image source, type) -> pool index, for dedup

    // Render the asset's default scene (or scene 0); fall back to walking every node if none is named.
    const int sceneIndex = gm.defaultScene >= 0 ? gm.defaultScene : (gm.scenes.empty() ? -1 : 0);
    if (sceneIndex >= 0)
        for (int root : gm.scenes[sceneIndex].nodes)
            processNode(gm, root, glm::mat4(1.0f), flipUVs, out, seen);
    else
        for (size_t i = 0; i < gm.nodes.size(); ++i)
            processNode(gm, (int)i, glm::mat4(1.0f), flipUVs, out, seen);

    out.valid = true;
    return out;
}

}  // namespace gltf
