// gltf_tangents — offline tool: bake per-vertex TANGENT attributes into a glTF that lacks them, so the
// engine doesn't have to regenerate them on every load (it still can, as a fallback). Non-destructive:
//
//     gltf_tangents <model.gltf|.glb>   ->   <model>.tangents.gltf
//
// The output is a single self-contained .gltf with buffers EMBEDDED as base64 (so we never overwrite the
// source .gltf/.bin) and images left as EXTERNAL references (textures are not touched, and relative paths
// still resolve since the output sits next to the source).
//
// Only primitives that have POSITION + NORMAL + TEXCOORD_0 but no TANGENT are modified. Tangents use
// Lengyel's method (per-triangle accumulation + Gram-Schmidt) including the handedness (.w) sign, per the
// glTF tangent convention.
//
// Standalone TU: it compiles its own copy of tinygltf (no link against the engine). Build via the
// Makefile's `tools` target.

// tinygltf needs stb_image to decode images on load, but the vendored tinygltf-release dir doesn't ship
// stb_image.h. So (like the engine's src/Core/gltf_loader.cpp) we feed it the engine's stb_image.h via
// TINYGLTF_NO_INCLUDE_STB_IMAGE. This TU is standalone, so it also compiles the stb_image implementation.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT                   // harmless on non-MSVC; silences an stb write guard
#include "Core/Engine/Helpers/stb_image.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE          // we never re-encode images; keep them external + untouched
#define TINYGLTF_NO_INCLUDE_STB_IMAGE        // use the stb_image included above, not tinygltf's bundled copy
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Read accessor element `i`, component `c`, as float (handles the float + normalized-integer types glTF
// allows for vertex attributes — enough for POSITION/NORMAL/TEXCOORD).
float readF(const tinygltf::Model& m, int accIdx, size_t i, int c) {
    const tinygltf::Accessor&   a   = m.accessors[accIdx];
    const tinygltf::BufferView& bv  = m.bufferViews[a.bufferView];
    const tinygltf::Buffer&     buf = m.buffers[bv.buffer];
    const int    comps  = tinygltf::GetNumComponentsInType(a.type);
    const int    cs     = tinygltf::GetComponentSizeInBytes(a.componentType);
    const size_t stride = a.ByteStride(bv) ? (size_t)a.ByteStride(bv) : (size_t)comps * cs;
    const unsigned char* p = buf.data.data() + bv.byteOffset + a.byteOffset + i * stride + (size_t)c * cs;
    switch (a.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT:          { float v; std::memcpy(&v, p, 4); return v; }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { unsigned short v; std::memcpy(&v, p, 2); return a.normalized ? v / 65535.0f : (float)v; }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  { unsigned char v = *p;                    return a.normalized ? v / 255.0f   : (float)v; }
        case TINYGLTF_COMPONENT_TYPE_SHORT:          { short v; std::memcpy(&v, p, 2);          return a.normalized ? std::max(v / 32767.0f, -1.0f) : (float)v; }
        case TINYGLTF_COMPONENT_TYPE_BYTE:           { signed char v = (signed char)*p;         return a.normalized ? std::max(v / 127.0f, -1.0f) : (float)v; }
        default: return 0.0f;
    }
}

unsigned int readIdx(const tinygltf::Model& m, int accIdx, size_t i) {
    const tinygltf::Accessor&   a   = m.accessors[accIdx];
    const tinygltf::BufferView& bv  = m.bufferViews[a.bufferView];
    const tinygltf::Buffer&     buf = m.buffers[bv.buffer];
    const int    cs     = tinygltf::GetComponentSizeInBytes(a.componentType);
    const size_t stride = a.ByteStride(bv) ? (size_t)a.ByteStride(bv) : (size_t)cs;
    const unsigned char* p = buf.data.data() + bv.byteOffset + a.byteOffset + i * stride;
    switch (a.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  return *p;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: { unsigned short v; std::memcpy(&v, p, 2); return v; }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   { unsigned int   v; std::memcpy(&v, p, 4); return v; }
        default: return 0;
    }
}

bool has(const tinygltf::Primitive& p, const char* a) { return p.attributes.find(a) != p.attributes.end(); }

// Append `floats` to buffer 0 (4-byte aligned) and return a new FLOAT accessor of the given type/count
// pointing at the appended region.
int appendAccessor(tinygltf::Model& m, const std::vector<float>& floats, int type, size_t count) {
    tinygltf::Buffer& buf = m.buffers[0];
    while (buf.data.size() % 4 != 0) buf.data.push_back(0);   // 4-byte alignment for float data
    const size_t byteOffset = buf.data.size();
    const size_t byteLen    = floats.size() * sizeof(float);
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(floats.data());
    buf.data.insert(buf.data.end(), raw, raw + byteLen);

    tinygltf::BufferView bv;
    bv.buffer     = 0;
    bv.byteOffset = byteOffset;
    bv.byteLength = byteLen;
    bv.target     = TINYGLTF_TARGET_ARRAY_BUFFER;
    m.bufferViews.push_back(bv);

    tinygltf::Accessor acc;
    acc.bufferView    = (int)m.bufferViews.size() - 1;
    acc.byteOffset    = 0;
    acc.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    acc.count         = count;
    acc.type          = type;
    acc.normalized    = false;
    m.accessors.push_back(acc);
    return (int)m.accessors.size() - 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: gltf_tangents <model.gltf|.glb>\n"
                     "  Writes <model>.tangents.gltf with TANGENT attributes baked in.\n";
        return 1;
    }
    const std::string path = argv[1];
    const size_t dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });

    tinygltf::Model m;
    tinygltf::TinyGLTF ctx;
    std::string err, warn;
    const bool ok = (ext == "glb") ? ctx.LoadBinaryFromFile(&m, &err, &warn, path)
                                   : ctx.LoadASCIIFromFile(&m, &err, &warn, path);
    if (!warn.empty()) std::cout << "warning: " << warn << "\n";
    if (!ok)            { std::cerr << "error: failed to load " << path << ": " << err << "\n"; return 1; }
    if (m.buffers.empty()) { std::cerr << "error: model has no buffers\n"; return 1; }

    int fixed = 0, already = 0, skipped = 0;
    for (tinygltf::Mesh& mesh : m.meshes) {
        for (tinygltf::Primitive& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) { ++skipped; continue; }
            if (has(prim, "TANGENT")) { ++already; continue; }
            if (!has(prim, "POSITION") || !has(prim, "NORMAL") || !has(prim, "TEXCOORD_0")) { ++skipped; continue; }

            const int    posA = prim.attributes.at("POSITION");
            const int    nrmA = prim.attributes.at("NORMAL");
            const int    uvA  = prim.attributes.at("TEXCOORD_0");
            const size_t vc   = m.accessors[posA].count;

            std::vector<glm::vec3> P(vc), N(vc);
            std::vector<glm::vec2> UV(vc);
            for (size_t i = 0; i < vc; ++i) {
                P[i]  = glm::vec3(readF(m, posA, i, 0), readF(m, posA, i, 1), readF(m, posA, i, 2));
                N[i]  = glm::vec3(readF(m, nrmA, i, 0), readF(m, nrmA, i, 1), readF(m, nrmA, i, 2));
                UV[i] = glm::vec2(readF(m, uvA, i, 0),  readF(m, uvA, i, 1));
            }

            std::vector<unsigned int> idx;
            if (prim.indices >= 0) {
                const size_t c = m.accessors[prim.indices].count;
                idx.resize(c);
                for (size_t i = 0; i < c; ++i) idx[i] = readIdx(m, prim.indices, i);
            } else {
                idx.resize(vc);
                for (size_t i = 0; i < vc; ++i) idx[i] = (unsigned int)i;
            }

            // Lengyel accumulation: sdir -> tangent, tdir -> bitangent (used only for the .w handedness).
            std::vector<glm::vec3> tan(vc, glm::vec3(0.0f)), bit(vc, glm::vec3(0.0f));
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                const unsigned int i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
                if (i0 >= vc || i1 >= vc || i2 >= vc) continue;
                const glm::vec3 e1 = P[i1] - P[i0], e2 = P[i2] - P[i0];
                const glm::vec2 d1 = UV[i1] - UV[i0], d2 = UV[i2] - UV[i0];
                const float denom = d1.x * d2.y - d2.x * d1.y;
                const float ad = denom < 0.0f ? -denom : denom;
                if (ad < 1e-12f) continue;                       // degenerate UV triangle
                const float r = 1.0f / denom;
                const glm::vec3 sdir = (d2.y * e1 - d1.y * e2) * r;
                const glm::vec3 tdir = (d1.x * e2 - d2.x * e1) * r;
                tan[i0] += sdir; tan[i1] += sdir; tan[i2] += sdir;
                bit[i0] += tdir; bit[i1] += tdir; bit[i2] += tdir;
            }

            std::vector<float> out;
            out.reserve(vc * 4);
            for (size_t i = 0; i < vc; ++i) {
                const glm::vec3 n = glm::normalize(N[i]);
                glm::vec3 T = tan[i] - n * glm::dot(n, tan[i]);   // Gram-Schmidt orthogonalize
                const float len = glm::length(T);
                T = (len > 1e-8f) ? T / len : glm::vec3(1.0f, 0.0f, 0.0f);
                const float w = (glm::dot(glm::cross(n, T), bit[i]) < 0.0f) ? -1.0f : 1.0f;  // handedness
                out.push_back(T.x); out.push_back(T.y); out.push_back(T.z); out.push_back(w);
            }

            prim.attributes["TANGENT"] = appendAccessor(m, out, TINYGLTF_TYPE_VEC4, vc);
            ++fixed;
        }
    }

    if (fixed == 0) {
        std::cout << "No primitives needed tangents (already had=" << already
                  << ", skipped=" << skipped << "). Nothing written.\n";
        return 0;
    }

    const std::string base = (dot == std::string::npos) ? path : path.substr(0, dot);
    const std::string out  = base + ".tangents.gltf";
    // embedBuffers=true => self-contained .gltf (never clobbers the source .bin); embedImages=false =>
    // images stay external and untouched (relative paths resolve from the same directory).
    if (!ctx.WriteGltfSceneToFile(&m, out, /*embedImages=*/false, /*embedBuffers=*/true,
                                  /*prettyPrint=*/true, /*writeBinary=*/false)) {
        std::cerr << "error: failed to write " << out << "\n";
        return 1;
    }
    std::cout << "Wrote " << out << "  (tangents baked into " << fixed << " primitive(s); already had="
              << already << ", skipped=" << skipped << ")\n";
    return 0;
}
