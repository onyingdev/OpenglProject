#pragma once
//
// Dedicated glTF 2.0 importer — built against the Khronos glTF 2.0 specification
// (vendor/glTF/glTF-main: spec + JSON schema), NOT a third-party loader. It uses only what the engine
// already vendors: nlohmann/json to parse the .gltf document and stb_image to decode images. Produces
// the engine's backend-agnostic ModelData, so it slots into the exact same async-parse + streamed GPU
// upload path as the Assimp loader (Model::parse dispatches .gltf/.glb here).
//
// GL-FREE: this runs on a worker thread. It only reads files, parses JSON, copies bytes out of the
// binary buffers via accessors/bufferViews, and decodes image pixels — no OpenGL calls. The GPU upload
// (and sRGB-vs-linear internal-format choice, driven by TextureData::colorSpace) happens later on the
// render thread in model.h.
//
// Supported (glTF 2.0 core, metallic-roughness): external .bin and base64 data: buffers, embedded
// bufferView / data: / external-file images, POSITION/NORMAL/TEXCOORD_0/TANGENT + indices, per-primitive
// materials (baseColor/metallicRoughness/normal/occlusion/emissive + factors, alphaMode, doubleSided),
// and tangent generation when a normal map is present but TANGENT is not.
//
// Deferred / log-and-skip: GLB binary container, Draco compression, animation, skins, morph targets,
// cameras, sparse accessors, secondary UV sets, and KHR material extensions beyond core metallic-rough.

#include "Render/Assets/ModelData.h"
#include "Core/Engine/Helpers/stb_image.h"
#include <nlohmann/json.hpp>

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

class GltfLoader {
public:
    // Parse a .gltf file into ModelData. On any hard failure logs and returns an invalid ModelData
    // (valid == false), matching Model::parse's contract so the caller leaves an empty (no-op) model.
    static ModelData load(const std::string& path)
    {
        using nlohmann::json;
        ModelData model;

        std::string text;
        if (!readFileText(path, text)) {
            std::cout << "ERROR::GLTF:: cannot open " << path << std::endl;
            return model;
        }

        json j;
        try { j = json::parse(text); }
        catch (const std::exception& e) {
            std::cout << "ERROR::GLTF:: JSON parse failed for " << path << ": " << e.what() << std::endl;
            return model;
        }

        model.directory = directoryOf(path);

        // Resolve every buffer up front (external .bin relative to the model dir, or base64 data: URIs).
        std::vector<std::vector<unsigned char>> buffers;
        if (j.contains("buffers"))
            for (const auto& b : j["buffers"])
                buffers.push_back(loadBuffer(b, model.directory));

        // Build all materials first (this also decodes + pools every referenced image with the right
        // type/color-space), so a primitive only has to point at its material by index afterwards.
        std::unordered_map<std::string, int> texCache;   // "imgIdx|typeName" -> ModelData.textures index
        if (j.contains("materials"))
            for (const auto& mj : j["materials"])
                model.materials.push_back(buildMaterial(mj, j, buffers, model, texCache));

        // Walk meshes -> primitives. One MeshData per primitive.
        if (j.contains("meshes"))
            for (const auto& meshj : j["meshes"])
                if (meshj.contains("primitives"))
                    for (const auto& prim : meshj["primitives"])
                        appendPrimitive(prim, j, buffers, model);

        model.valid = !model.meshes.empty();
        if (!model.valid)
            std::cout << "ERROR::GLTF:: no drawable primitives in " << path << std::endl;
        return model;
    }

private:
    // ---- small file / path / string helpers -------------------------------------------------------

    static bool readFileText(const std::string& path, std::string& out)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss; ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    static bool readFileBinary(const std::string& path, std::vector<unsigned char>& out)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return false;
        const std::streamsize sz = f.tellg();
        if (sz <= 0) return false;
        f.seekg(0);
        out.resize((size_t)sz);
        return (bool)f.read(reinterpret_cast<char*>(out.data()), sz);
    }

    // Directory of a path, with backslashes normalized to '/' (the old find_last_of('/') was
    // forward-slash-only and broke on Windows-style paths).
    static std::string directoryOf(const std::string& path)
    {
        std::string p = path;
        for (char& c : p) if (c == '\\') c = '/';
        const size_t slash = p.find_last_of('/');
        return (slash == std::string::npos) ? std::string(".") : p.substr(0, slash);
    }

    // Decode the %XX escapes glTF URIs may contain (e.g. "%20" -> space) and normalize separators.
    static std::string decodeUri(const std::string& uri)
    {
        std::string out;
        out.reserve(uri.size());
        for (size_t i = 0; i < uri.size(); ++i) {
            if (uri[i] == '%' && i + 2 < uri.size()) {
                auto hex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                const int hi = hex(uri[i + 1]), lo = hex(uri[i + 2]);
                if (hi >= 0 && lo >= 0) { out.push_back((char)((hi << 4) | lo)); i += 2; continue; }
            }
            out.push_back(uri[i] == '\\' ? '/' : uri[i]);
        }
        return out;
    }

    static bool startsWith(const std::string& s, const char* prefix)
    {
        const size_t n = std::strlen(prefix);
        return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
    }

    // Minimal RFC 4648 base64 decode (for "data:...;base64,...." buffer/image URIs).
    static std::vector<unsigned char> base64Decode(const std::string& in)
    {
        auto val = [](unsigned char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };
        std::vector<unsigned char> out;
        out.reserve(in.size() * 3 / 4);
        int acc = 0, bits = 0;
        for (unsigned char c : in) {
            if (c == '=' ) break;
            const int v = val(c);
            if (v < 0) continue;                 // skip whitespace/newlines
            acc = (acc << 6) | v; bits += 6;
            if (bits >= 8) { bits -= 8; out.push_back((unsigned char)((acc >> bits) & 0xFF)); }
        }
        return out;
    }

    // Bytes of a "data:[mediatype][;base64],<payload>" URI (returns the decoded payload), else empty.
    static std::vector<unsigned char> dataUriBytes(const std::string& uri)
    {
        const size_t comma = uri.find(',');
        if (comma == std::string::npos) return {};
        const std::string meta = uri.substr(0, comma);
        const std::string payload = uri.substr(comma + 1);
        if (meta.find(";base64") != std::string::npos) return base64Decode(payload);
        return std::vector<unsigned char>(payload.begin(), payload.end());   // raw (rare)
    }

    // ---- buffers ----------------------------------------------------------------------------------

    static std::vector<unsigned char> loadBuffer(const nlohmann::json& b, const std::string& dir)
    {
        std::vector<unsigned char> bytes;
        if (!b.contains("uri")) {   // GLB-embedded buffer (no uri) — not supported yet
            std::cout << "WARN::GLTF:: buffer without uri (GLB) unsupported; skipping" << std::endl;
            return bytes;
        }
        const std::string uri = b["uri"].get<std::string>();
        if (startsWith(uri, "data:")) return dataUriBytes(uri);
        const std::string file = dir + "/" + decodeUri(uri);
        if (!readFileBinary(file, bytes))
            std::cout << "ERROR::GLTF:: cannot read buffer " << file << std::endl;
        return bytes;
    }

    // ---- accessors / bufferViews ------------------------------------------------------------------

    static size_t componentByteSize(int ct)
    {
        switch (ct) {
            case 5120: case 5121: return 1;   // (unsigned) byte
            case 5122: case 5123: return 2;   // (unsigned) short
            case 5125: case 5126: return 4;   // unsigned int / float
            default: return 0;
        }
    }
    static int numComponents(const std::string& type)
    {
        if (type == "SCALAR") return 1;
        if (type == "VEC2")   return 2;
        if (type == "VEC3")   return 3;
        if (type == "VEC4")   return 4;
        if (type == "MAT2")   return 4;
        if (type == "MAT3")   return 9;
        if (type == "MAT4")   return 16;
        return 0;
    }

    // Read one component as float, honoring the glTF normalized-integer convention.
    static float readComponentAsFloat(const unsigned char* p, int ct, bool normalized)
    {
        switch (ct) {
            case 5126: { float v; std::memcpy(&v, p, 4); return v; }
            case 5125: { uint32_t v; std::memcpy(&v, p, 4); return (float)v; }
            case 5123: { uint16_t v; std::memcpy(&v, p, 2); return normalized ? v / 65535.0f : (float)v; }
            case 5121: {  uint8_t v = *p;                   return normalized ? v / 255.0f   : (float)v; }
            case 5122: { int16_t v; std::memcpy(&v, p, 2);  return normalized ? std::max(v / 32767.0f, -1.0f) : (float)v; }
            case 5120: {  int8_t v = (int8_t)*p;            return normalized ? std::max(v / 127.0f,   -1.0f) : (float)v; }
            default: return 0.0f;
        }
    }

    // Pull an accessor's data out as tightly-packed floats (count * numComponents). Handles component
    // type conversion, normalization, and interleaved byteStride. `compsOut` returns the per-element
    // component count. Returns empty on anything unsupported (sparse accessors, missing bufferView).
    static std::vector<float> readAccessorFloats(const nlohmann::json& j,
                                                 const std::vector<std::vector<unsigned char>>& buffers,
                                                 int accessorIdx, int& compsOut)
    {
        compsOut = 0;
        if (accessorIdx < 0 || !j.contains("accessors") || accessorIdx >= (int)j["accessors"].size())
            return {};
        const auto& acc = j["accessors"][accessorIdx];
        if (acc.contains("sparse")) { std::cout << "WARN::GLTF:: sparse accessor skipped" << std::endl; return {}; }
        if (!acc.contains("bufferView")) return {};   // (would be all-zero; nothing useful to read)

        const int ct = acc.value("componentType", 0);
        const std::string type = acc.value("type", std::string());
        const int nc = numComponents(type);
        const size_t count = acc.value("count", 0);
        const bool normalized = acc.value("normalized", false);
        const size_t compSize = componentByteSize(ct);
        if (nc == 0 || compSize == 0 || count == 0) return {};

        const int bvIdx = acc["bufferView"].get<int>();
        if (!j.contains("bufferViews") || bvIdx < 0 || bvIdx >= (int)j["bufferViews"].size()) return {};
        const auto& bv = j["bufferViews"][bvIdx];
        const int bufIdx = bv.value("buffer", -1);
        if (bufIdx < 0 || bufIdx >= (int)buffers.size()) return {};
        const std::vector<unsigned char>& buf = buffers[bufIdx];

        const size_t elemSize = compSize * (size_t)nc;
        size_t stride = bv.value("byteStride", (size_t)0);
        if (stride == 0) stride = elemSize;
        const size_t base = bv.value("byteOffset", (size_t)0) + acc.value("byteOffset", (size_t)0);

        if (base + (count - 1) * stride + elemSize > buf.size()) {
            std::cout << "ERROR::GLTF:: accessor " << accessorIdx << " out of buffer range" << std::endl;
            return {};
        }

        std::vector<float> out(count * (size_t)nc);
        for (size_t i = 0; i < count; ++i) {
            const unsigned char* e = buf.data() + base + i * stride;
            for (int c = 0; c < nc; ++c)
                out[i * nc + c] = readComponentAsFloat(e + c * compSize, ct, normalized);
        }
        compsOut = nc;
        return out;
    }

    static std::vector<unsigned int> readIndices(const nlohmann::json& j,
                                                 const std::vector<std::vector<unsigned char>>& buffers,
                                                 int accessorIdx)
    {
        std::vector<unsigned int> out;
        if (accessorIdx < 0 || !j.contains("accessors") || accessorIdx >= (int)j["accessors"].size())
            return out;
        const auto& acc = j["accessors"][accessorIdx];
        if (!acc.contains("bufferView")) return out;
        const int ct = acc.value("componentType", 0);
        const size_t count = acc.value("count", 0);
        const size_t compSize = componentByteSize(ct);
        if (compSize == 0 || count == 0) return out;

        const int bvIdx = acc["bufferView"].get<int>();
        if (!j.contains("bufferViews") || bvIdx < 0 || bvIdx >= (int)j["bufferViews"].size()) return out;
        const auto& bv = j["bufferViews"][bvIdx];
        const int bufIdx = bv.value("buffer", -1);
        if (bufIdx < 0 || bufIdx >= (int)buffers.size()) return out;
        const std::vector<unsigned char>& buf = buffers[bufIdx];
        size_t stride = bv.value("byteStride", (size_t)0);
        if (stride == 0) stride = compSize;
        const size_t base = bv.value("byteOffset", (size_t)0) + acc.value("byteOffset", (size_t)0);
        if (base + (count - 1) * stride + compSize > buf.size()) return out;

        out.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const unsigned char* p = buf.data() + base + i * stride;
            switch (ct) {
                case 5121: out[i] = *p; break;
                case 5123: { uint16_t v; std::memcpy(&v, p, 2); out[i] = v; break; }
                case 5125: { uint32_t v; std::memcpy(&v, p, 4); out[i] = v; break; }
                default: out[i] = 0; break;
            }
        }
        return out;
    }

    // ---- primitives -------------------------------------------------------------------------------

    static void appendPrimitive(const nlohmann::json& prim, const nlohmann::json& j,
                                const std::vector<std::vector<unsigned char>>& buffers, ModelData& model)
    {
        if (prim.value("mode", 4) != 4) {   // 4 == TRIANGLES; we only rasterize triangle lists
            std::cout << "WARN::GLTF:: non-triangle primitive (mode "
                      << prim.value("mode", 4) << ") skipped" << std::endl;
            return;
        }
        if (!prim.contains("attributes") || !prim["attributes"].contains("POSITION")) return;
        const auto& attr = prim["attributes"];

        int nc = 0;
        std::vector<float> pos = readAccessorFloats(j, buffers, attr["POSITION"].get<int>(), nc);
        if (pos.empty() || nc != 3) return;
        const size_t vcount = pos.size() / 3;

        std::vector<float> nrm, uv, tan;
        int ncN = 0, ncU = 0, ncT = 0;
        if (attr.contains("NORMAL"))     nrm = readAccessorFloats(j, buffers, attr["NORMAL"].get<int>(), ncN);
        if (attr.contains("TEXCOORD_0")) uv  = readAccessorFloats(j, buffers, attr["TEXCOORD_0"].get<int>(), ncU);
        if (attr.contains("TANGENT"))    tan = readAccessorFloats(j, buffers, attr["TANGENT"].get<int>(), ncT);

        MeshData md;
        md.vertices.resize(vcount);
        for (size_t i = 0; i < vcount; ++i) {
            Vertex v{};
            v.Position = glm::vec3(pos[i * 3 + 0], pos[i * 3 + 1], pos[i * 3 + 2]);
            if (ncN == 3) v.Normal = glm::vec3(nrm[i * 3 + 0], nrm[i * 3 + 1], nrm[i * 3 + 2]);
            if (ncU == 2) v.TexCoords = glm::vec2(uv[i * 2 + 0], uv[i * 2 + 1]);
            if (ncT == 4) {
                const glm::vec3 t(tan[i * 4 + 0], tan[i * 4 + 1], tan[i * 4 + 2]);
                v.Tangent   = t;
                v.Bitangent = glm::cross(v.Normal, t) * tan[i * 4 + 3];   // .w = handedness
            }
            md.vertices[i] = v;
        }

        if (prim.contains("indices"))
            md.indices = readIndices(j, buffers, prim["indices"].get<int>());
        if (md.indices.empty()) {                    // non-indexed: synthesize a sequential list
            md.indices.resize(vcount);
            for (size_t i = 0; i < vcount; ++i) md.indices[i] = (unsigned int)i;
        }

        // Material: point the mesh at the prebuilt Material (factors) and replay its texture pool indices
        // into the per-mesh list (the Blinn-Phong bind path uses these; base color binds as albedo).
        const int matIdx = prim.value("material", -1);
        md.materialIndex = matIdx;
        const bool hasNormalMap = (matIdx >= 0 && matIdx < (int)model.materials.size()
                                   && model.materials[matIdx].normalTex >= 0);
        if (matIdx >= 0 && matIdx < (int)model.materials.size()) {
            const Material& m = model.materials[matIdx];
            for (int idx : { m.baseColorTex, m.metallicRoughnessTex, m.normalTex, m.occlusionTex, m.emissiveTex })
                if (idx >= 0) md.textureIndices.push_back(idx);
        }

        // glTF may omit TANGENT; generate it when a normal map will actually use the tangent frame.
        if (ncT != 4 && hasNormalMap && ncU == 2 && ncN == 3)
            computeTangents(md.vertices, md.indices);

        model.meshes.push_back(std::move(md));
    }

    // Per-triangle tangent accumulation + Gram-Schmidt orthonormalization against the vertex normal.
    static void computeTangents(std::vector<Vertex>& verts, const std::vector<unsigned int>& idx)
    {
        std::vector<glm::vec3> acc(verts.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            const unsigned int i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];
            if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;
            const glm::vec3 e1 = verts[i1].Position - verts[i0].Position;
            const glm::vec3 e2 = verts[i2].Position - verts[i0].Position;
            const glm::vec2 d1 = verts[i1].TexCoords - verts[i0].TexCoords;
            const glm::vec2 d2 = verts[i2].TexCoords - verts[i0].TexCoords;
            const float denom = d1.x * d2.y - d2.x * d1.y;
            if (std::abs(denom) < 1e-12f) continue;
            const float f = 1.0f / denom;
            const glm::vec3 t = f * (d2.y * e1 - d1.y * e2);
            acc[i0] += t; acc[i1] += t; acc[i2] += t;
        }
        for (size_t v = 0; v < verts.size(); ++v) {
            const glm::vec3 n = verts[v].Normal;
            glm::vec3 t = acc[v] - n * glm::dot(n, acc[v]);   // orthogonalize
            const float len = glm::length(t);
            t = (len > 1e-8f) ? t / len : glm::vec3(1.0f, 0.0f, 0.0f);
            verts[v].Tangent   = t;
            verts[v].Bitangent = glm::cross(n, t);
        }
    }

    // ---- materials & images -----------------------------------------------------------------------

    static Material buildMaterial(const nlohmann::json& mj, const nlohmann::json& j,
                                  const std::vector<std::vector<unsigned char>>& buffers,
                                  ModelData& model, std::unordered_map<std::string, int>& texCache)
    {
        Material m;
        if (mj.contains("pbrMetallicRoughness")) {
            const auto& pbr = mj["pbrMetallicRoughness"];
            if (pbr.contains("baseColorFactor")) {
                const auto& a = pbr["baseColorFactor"];
                if (a.size() == 4)
                    m.baseColorFactor = glm::vec4(a[0].get<float>(), a[1].get<float>(),
                                                  a[2].get<float>(), a[3].get<float>());
            }
            m.metallicFactor  = pbr.value("metallicFactor", 1.0f);
            m.roughnessFactor = pbr.value("roughnessFactor", 1.0f);
            m.baseColorTex = addTexture(pbr, "baseColorTexture", "texture_diffuse",
                                        ColorSpace::sRGB, j, buffers, model, texCache);
            m.metallicRoughnessTex = addTexture(pbr, "metallicRoughnessTexture", "texture_metallic_roughness",
                                                ColorSpace::Linear, j, buffers, model, texCache);
        }
        if (mj.contains("normalTexture")) {
            m.normalScale = mj["normalTexture"].value("scale", 1.0f);
            m.normalTex = addTexture(mj, "normalTexture", "texture_normal",
                                     ColorSpace::Linear, j, buffers, model, texCache);
        }
        if (mj.contains("occlusionTexture")) {
            m.occlusionStrength = mj["occlusionTexture"].value("strength", 1.0f);
            m.occlusionTex = addTexture(mj, "occlusionTexture", "texture_occlusion",
                                        ColorSpace::Linear, j, buffers, model, texCache);
        }
        if (mj.contains("emissiveFactor")) {
            const auto& e = mj["emissiveFactor"];
            if (e.size() == 3)
                m.emissiveFactor = glm::vec3(e[0].get<float>(), e[1].get<float>(), e[2].get<float>());
        }
        m.emissiveTex = addTexture(mj, "emissiveTexture", "texture_emissive",
                                   ColorSpace::sRGB, j, buffers, model, texCache);

        const std::string am = mj.value("alphaMode", std::string("OPAQUE"));
        m.alphaMode   = (am == "MASK") ? Material::Mask : (am == "BLEND") ? Material::Blend : Material::Opaque;
        m.alphaCutoff = mj.value("alphaCutoff", 0.5f);
        m.doubleSided = mj.value("doubleSided", false);
        return m;
    }

    // Resolve material.<slot>.index -> glTF texture -> image, decode (deduped by image+type), pool it
    // with the given sampler family + color space, and return its ModelData.textures index (-1 = none).
    static int addTexture(const nlohmann::json& owner, const char* slot, const char* typeName,
                          ColorSpace cs, const nlohmann::json& j,
                          const std::vector<std::vector<unsigned char>>& buffers,
                          ModelData& model, std::unordered_map<std::string, int>& texCache)
    {
        if (!owner.contains(slot)) return -1;
        const int texIdx = owner[slot].value("index", -1);
        if (texIdx < 0 || !j.contains("textures") || texIdx >= (int)j["textures"].size()) return -1;
        const auto& tex = j["textures"][texIdx];
        const int imgIdx = tex.value("source", -1);
        if (imgIdx < 0 || !j.contains("images") || imgIdx >= (int)j["images"].size()) {
            std::cout << "WARN::GLTF:: texture " << texIdx << " has no decodable image source ("
                      << typeName << ")" << std::endl;
            return -1;
        }

        const std::string key = std::to_string(imgIdx) + "|" + typeName;
        auto it = texCache.find(key);
        if (it != texCache.end()) return it->second;

        TextureData td = decodeImage(j, j["images"][imgIdx], buffers, model.directory);
        td.type       = typeName;
        td.colorSpace = cs;
        model.textures.push_back(std::move(td));
        const int poolIdx = (int)model.textures.size() - 1;
        texCache[key] = poolIdx;
        return poolIdx;
    }

    // Decode one glTF image to pixels. Three sources: external file (uri), base64 (data: uri), or an
    // embedded bufferView (mimeType + bytes inside a buffer). Empty pixels => upload uses the fallback.
    static TextureData decodeImage(const nlohmann::json& j, const nlohmann::json& img,
                                   const std::vector<std::vector<unsigned char>>& buffers,
                                   const std::string& dir)
    {
        TextureData td;
        int w = 0, h = 0, ch = 0;
        unsigned char* pixels = nullptr;

        if (img.contains("uri")) {
            const std::string uri = img["uri"].get<std::string>();
            if (startsWith(uri, "data:")) {
                const std::vector<unsigned char> bytes = dataUriBytes(uri);
                if (!bytes.empty())
                    pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 0);
                td.path = "data:";
            } else {
                const std::string file = dir + "/" + decodeUri(uri);
                pixels = stbi_load(file.c_str(), &w, &h, &ch, 0);
                td.path = uri;
                if (!pixels) std::cout << "Texture failed to load at path: " << file << std::endl;
            }
        } else if (img.contains("bufferView")) {
            const int bvIdx = img["bufferView"].get<int>();
            if (j.contains("bufferViews") && bvIdx >= 0 && bvIdx < (int)j["bufferViews"].size()) {
                const auto& bv = j["bufferViews"][bvIdx];
                const int bufIdx = bv.value("buffer", -1);
                if (bufIdx >= 0 && bufIdx < (int)buffers.size()) {
                    const std::vector<unsigned char>& buf = buffers[bufIdx];
                    const size_t off = bv.value("byteOffset", (size_t)0);
                    const size_t len = bv.value("byteLength", (size_t)0);
                    if (len > 0 && off + len <= buf.size())
                        pixels = stbi_load_from_memory(buf.data() + off, (int)len, &w, &h, &ch, 0);
                }
            }
            td.path = "*img-bufferView";
            if (!pixels) std::cout << "WARN::GLTF:: embedded image decode failed" << std::endl;
        }

        if (pixels) {
            td.width = w; td.height = h; td.channels = ch;
            td.pixels.assign(pixels, pixels + (size_t)w * h * ch);
            stbi_image_free(pixels);
        }
        return td;
    }
};
