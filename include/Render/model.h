#ifndef MODEL_H
#define MODEL_H

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Core/Engine/Helpers/stb_image.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Render/mesh.h"
#include "Render/API/IShader.h"
#include "Render/API/IModel.h"
#include "Render/Assets/ModelData.h"   // backend-agnostic CPU asset data (parse output)
#include "Render/Assets/GLTFLoader.h"  // tinygltf path for .gltf/.glb (Assimp handles the rest)

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>
#include <cstring>
#include <utility>
using namespace std;

// OpenGL implementation of IModel: loads via Assimp and owns its meshes + textures.
//
// Loading is split into two stages: parse() reads + decodes a file into a backend-agnostic ModelData
// touching NO GL (so it can run on a worker thread), and uploadFrom() turns that ModelData into GPU
// meshes/textures on the render thread. The sync Model(path) ctor just runs both back-to-back.
class Model : public IModel
{
public:
    // model data
    vector<Texture> textures_loaded;	// stores all the textures loaded so far, optimization to make sure textures aren't loaded more than once.
    vector<Mesh>    meshes;
    string directory;
    bool gammaCorrection;
    bool flipUVs = false;

    // constructor, expects a filepath to a 3D model.
    // flipUVs: vertically flip UVs at load. Set true for models authored with the
    // LearnOpenGL/UE convention (e.g. backpack); leave false otherwise. Combine with
    // stbi_set_flip_vertically_on_load to choose the correct net orientation.
    // Sync load: parse on the calling thread, then upload here. Same signature/behavior as before —
    // the delegating ctor just routes through the new two-stage path.
    Model(string const &path, bool gamma = false, bool flipUVs = false)
        : Model(parse(path, gamma, flipUVs)) {}

    // Async-friendly load: upload an already-parsed ModelData. MUST run on the render (GL) thread.
    // Phase 2's AssetManager calls parse() on a worker, then constructs the Model through this ctor.
    explicit Model(ModelData &&data) { uploadFrom(std::move(data)); }

    // Streamed load: allocate the GPU buffers now (sized from `data`) but upload the geometry/textures
    // incrementally via uploadStep() across frames — avoids the single-frame upload spike for a heavy
    // model. The model is NOT drawable until uploadStep() reports done (its handle stays not-ready).
    struct StreamTag {};
    Model(ModelData &&data, StreamTag) { beginStreamed(std::move(data)); }

    static std::shared_ptr<Model> createStreamed(ModelData &&data) {
        return std::make_shared<Model>(std::move(data), StreamTag{});
    }

    // Advance the streamed upload by up to `budget` bytes (decremented by what's consumed). Returns
    // true once geometry + textures are fully uploaded and the model is drawable; a non-streamed or
    // finished model returns true immediately. Render (GL) thread only.
    bool uploadStep(size_t &budget)
    {
        if (!m_streaming) return true;

        // Phase 1: geometry chunks, spread across this model's meshes within the budget.
        for (size_t i = 0; i < meshes.size() && budget > 0; ++i) {
            if (meshes[i].geometryDone()) continue;
            const size_t used = meshes[i].uploadGeometryStep(
                m_data.meshes[i].vertices.data(), m_data.meshes[i].indices.data(), budget);
            budget = (used >= budget) ? 0 : budget - used;
        }
        for (const Mesh &m : meshes) if (!m.geometryDone()) return false;

        // Phase 2: textures — one per step (a glTexImage2D is atomic), charged against the budget.
        while (m_texCursor < m_data.textures.size() && budget > 0) {
            const TextureData &td = m_data.textures[m_texCursor];
            Texture t; t.id = uploadTexture(td); t.type = td.type; t.path = td.path;
            m_texPool.push_back(t);
            textures_loaded.push_back(t);
            const size_t cost = td.pixels.empty() ? size_t(1) : td.pixels.size();
            budget = (cost >= budget) ? 0 : budget - cost;
            ++m_texCursor;
        }
        if (m_texCursor < m_data.textures.size()) return false;

        // Phase 3: finalize — resolve each mesh's texture handles from the pool, free the CPU data.
        for (size_t i = 0; i < meshes.size(); ++i) {
            vector<Texture> meshTextures;
            meshTextures.reserve(m_data.meshes[i].textureIndices.size());
            for (int idx : m_data.meshes[i].textureIndices)
                if (idx >= 0 && idx < (int)m_texPool.size()) meshTextures.push_back(m_texPool[idx]);
            meshes[i].textures = std::move(meshTextures);
        }
        m_data = ModelData{};   // free CPU geometry + pixels (RAM win over the retained sync path)
        m_streaming = false;
        return true;
    }

    // Mesh is move-only and Model owns GPU texture IDs, so block copies and define moves.
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    Model(Model&& other) noexcept
        : textures_loaded(std::move(other.textures_loaded)),
          meshes(std::move(other.meshes)),
          directory(std::move(other.directory)),
          gammaCorrection(other.gammaCorrection),
          flipUVs(other.flipUVs),
          m_streaming(other.m_streaming),
          m_data(std::move(other.m_data)),
          m_texPool(std::move(other.m_texPool)),
          m_texCursor(other.m_texCursor)
    {
        other.textures_loaded.clear(); // moved-from vector is valid but unspecified; make sure dtor sees nothing to free.
        other.m_streaming = false;
    }

    Model& operator=(Model&& other) noexcept
    {
        if (this != &other) {
            releaseTextures();
            textures_loaded = std::move(other.textures_loaded);
            meshes          = std::move(other.meshes);
            directory       = std::move(other.directory);
            gammaCorrection = other.gammaCorrection;
            flipUVs         = other.flipUVs;
            m_streaming     = other.m_streaming;
            m_data          = std::move(other.m_data);
            m_texPool       = std::move(other.m_texPool);
            m_texCursor     = other.m_texCursor;
            other.textures_loaded.clear();
            other.m_streaming = false;
        }
        return *this;
    }

    ~Model()
    {
        releaseTextures();
    }

    // draws the model, and thus all its meshes
    void Draw(IShader &shader) override
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
            meshes[i].Draw(shader);
    }

    // Draw `count` copies of every mesh in one instanced call each (see Mesh::DrawInstanced). `pass`
    // selects the alpha class: each mesh draws only if it matches (Opaque skips BLEND meshes, etc.).
    void DrawInstanced(IShader &shader, const glm::mat4* instances, int count,
                       MaterialPass pass = MaterialPass::Opaque) override
    {
        for(unsigned int i = 0; i < meshes.size(); i++)
            meshes[i].DrawInstanced(shader, instances, count, pass);
    }

private:
    // ---- streamed-upload state (set by beginStreamed, consumed/cleared by uploadStep) ----
    bool            m_streaming = false;
    ModelData       m_data;       // retained parse output, streamed to the GPU then freed
    vector<Texture> m_texPool;    // GPU handles built incrementally during streamed texture upload
    size_t          m_texCursor = 0;

    void releaseTextures()
    {
        for (auto& tex : textures_loaded) {
            if (tex.id) glDeleteTextures(1, &tex.id);
            tex.id = 0;
        }
    }

    // Allocate the GPU buffers for a streamed upload (no data yet) and retain the source. The actual
    // geometry/texture upload happens later, in chunks, via uploadStep().
    void beginStreamed(ModelData &&data)
    {
        m_data          = std::move(data);
        directory       = m_data.directory;
        gammaCorrection = m_data.gammaCorrection;
        flipUVs         = m_data.flipUVs;
        if (!m_data.valid) { m_streaming = false; return; }   // parse failed: empty model

        m_streaming = true;
        meshes.reserve(m_data.meshes.size());
        for (const MeshData &md : m_data.meshes) {
            meshes.emplace_back(md.vertices.size(), md.indices.size(), md.baseColor, MeshStreamTag{});
            applyMaterialFactors(meshes.back(), md);
        }
        m_texPool.reserve(m_data.textures.size());
        m_texCursor = 0;
    }

    // ---- GPU upload stage (render/GL thread only) ----
    // Turn a parsed ModelData into GPU meshes + textures. Uploads the de-duplicated texture pool once,
    // then builds each Mesh, resolving its texture indices to the uploaded GL handles.
    void uploadFrom(ModelData &&data)
    {
        directory       = std::move(data.directory);
        gammaCorrection = data.gammaCorrection;
        flipUVs         = data.flipUVs;
        if (!data.valid) return;   // parse failed: leave an empty model (Draw is a no-op)

        // Upload each decoded texture once; keep a parallel pool of GPU handles to resolve per-mesh.
        vector<Texture> pool;
        pool.reserve(data.textures.size());
        for (const TextureData &td : data.textures)
        {
            Texture t;
            t.id   = uploadTexture(td);
            t.type = td.type;
            t.path = td.path;
            pool.push_back(t);
            textures_loaded.push_back(t);   // model-wide dedup record (kept for parity / future reuse)
        }

        // Build each mesh, mapping its texture indices to the uploaded handles.
        meshes.reserve(data.meshes.size());
        for (MeshData &m : data.meshes)
        {
            vector<Texture> meshTextures;
            meshTextures.reserve(m.textureIndices.size());
            for (int idx : m.textureIndices)
                if (idx >= 0 && idx < (int)pool.size()) meshTextures.push_back(pool[idx]);
            meshes.emplace_back(std::move(m.vertices), std::move(m.indices), std::move(meshTextures), m.baseColor);
            applyMaterialFactors(meshes.back(), m);
        }
    }

    // Copy the PBR metallic-roughness factors from a parsed MeshData onto its GPU Mesh (baseColor already
    // went through the ctor; the rest are plain scalars/vec3s set here).
    static void applyMaterialFactors(Mesh &mesh, const MeshData &md)
    {
        mesh.metallic          = md.metallic;
        mesh.roughness         = md.roughness;
        mesh.emissiveFactor    = md.emissiveFactor;
        mesh.occlusionStrength = md.occlusionStrength;
        mesh.normalScale       = md.normalScale;
        mesh.baseColorAlpha    = md.baseColorAlpha;
        mesh.alphaMode         = md.alphaMode;
        mesh.alphaCutoff       = md.alphaCutoff;
        mesh.doubleSided       = md.doubleSided;
    }

public:
    // ---- CPU parse stage (no GL — safe to run on a worker thread) ----
    // Read + decode a model from disk into backend-agnostic ModelData: Assimp for geometry, stb_image
    // for texture pixels. Touches no graphics API. The result is uploaded later via Model(ModelData&&).
    static ModelData parse(string const &path, bool gamma = false, bool flipUVs = false)
    {
        // glTF/glb go through tinygltf (gltf::Load), which produces the same backend-agnostic ModelData.
        // Everything else (OBJ/FBX/...) stays on Assimp below.
        if (gltf::isGLTF(path))
            return gltf::Load(path, gamma, flipUVs);

        ModelData model;
        model.gammaCorrection = gamma;
        model.flipUVs         = flipUVs;

        Assimp::Importer importer;
        unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace;
        if (flipUVs) flags |= aiProcess_FlipUVs;
        const aiScene *scene = importer.ReadFile(path, flags);
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
            return model;   // model.valid stays false
        }
        // TODO: change pathing method for searching other folders
        model.directory = path.substr(0, path.find_last_of('/'));
        processNode(scene->mRootNode, scene, model);
        model.valid = true;
        return model;
    }

private:
    // recursively walk the node tree, appending each mesh's MeshData to `model`.
    static void processNode(aiNode *node, const aiScene *scene, ModelData &model)
    {
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
            model.meshes.push_back(processMesh(mesh, scene, model));
        }
        for (unsigned int i = 0; i < node->mNumChildren; i++)
            processNode(node->mChildren[i], scene, model);
    }

    static MeshData processMesh(aiMesh *mesh, const aiScene *scene, ModelData &model)
    {
        MeshData out;
        out.vertices.reserve(mesh->mNumVertices);

        // walk through each of the mesh's vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            Vertex vertex{};   // zero-inits bone ids/weights (unused until skinning)
            glm::vec3 vector;  // assimp's vec3 doesn't convert to glm directly; stage through this
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;
            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            }
            if (mesh->mTextureCoords[0])   // does the mesh contain texture coordinates?
            {
                glm::vec2 vec;
                // assume one UV set (0); a vertex could hold up to 8.
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
                vector.x = mesh->mTangents[i].x;
                vector.y = mesh->mTangents[i].y;
                vector.z = mesh->mTangents[i].z;
                vertex.Tangent = vector;
                vector.x = mesh->mBitangents[i].x;
                vector.y = mesh->mBitangents[i].y;
                vector.z = mesh->mBitangents[i].z;
                vertex.Bitangent = vector;
            }
            else
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);

            out.vertices.push_back(vertex);
        }
        // each face is a triangle (post-Triangulate); gather its indices
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                out.indices.push_back(face.mIndices[j]);
        }

        // materials -> texture pool indices. Sampler-name convention: texture_diffuseN / _specularN /
        // _normalN / _heightN (N assigned at bind time in Mesh::bindTextures).
        aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

        // Flat diffuse color factor — used when the mesh has no diffuse map (multiplied with the map
        // otherwise, white = no-op). Mirrors the glTF baseColorFactor path; stays white if unset.
        aiColor4D diffuseColor;
        if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor) == AI_SUCCESS)
            out.baseColor = glm::vec3(diffuseColor.r, diffuseColor.g, diffuseColor.b);

        // Color textures (diffuse/base color) are sRGB; data maps (specular/normal/height) are linear, so
        // the new uploadTexture picks the right internal format and OBJ/FBX diffuse isn't double-dark.
        // 1. diffuse / base color (glTF & FBX expose base color under BASE_COLOR) — sRGB
        vector<int> diffuse = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse", ColorSpace::sRGB, model);
        if (diffuse.empty())
            diffuse = loadMaterialTextures(material, aiTextureType_BASE_COLOR, "texture_diffuse", ColorSpace::sRGB, model);
        out.textureIndices.insert(out.textureIndices.end(), diffuse.begin(), diffuse.end());
        // 2. specular (linear reflectance data)
        vector<int> specular = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular", ColorSpace::Linear, model);
        out.textureIndices.insert(out.textureIndices.end(), specular.begin(), specular.end());
        // 3. normal (NORMALS for glTF/FBX; OBJ's map_Bump arrives as HEIGHT, fall back to it) — linear
        vector<int> normal = loadMaterialTextures(material, aiTextureType_NORMALS, "texture_normal", ColorSpace::Linear, model);
        if (normal.empty())
            normal = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal", ColorSpace::Linear, model);
        out.textureIndices.insert(out.textureIndices.end(), normal.begin(), normal.end());
        // 4. height / displacement — linear
        vector<int> height = loadMaterialTextures(material, aiTextureType_DISPLACEMENT, "texture_height", ColorSpace::Linear, model);
        out.textureIndices.insert(out.textureIndices.end(), height.begin(), height.end());

        return out;
    }

    // Decode each texture of `type` into model.textures (de-duped by path) and return their indices.
    // `cs` tags the decoded texture's color space so the GPU upload picks an sRGB or linear format.
    static vector<int> loadMaterialTextures(aiMaterial *mat, aiTextureType type, string typeName, ColorSpace cs, ModelData &model)
    {
        vector<int> indices;
        for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
        {
            aiString str;
            mat->GetTexture(type, i, &str);
            // already decoded into the pool? reuse its index (avoids decoding the same file twice).
            int found = -1;
            for (unsigned int j = 0; j < model.textures.size(); j++)
                if (std::strcmp(model.textures[j].path.c_str(), str.C_Str()) == 0) { found = (int)j; break; }
            if (found >= 0) { indices.push_back(found); continue; }

            TextureData td = decodeTexture(str.C_Str(), model.directory);
            td.type       = typeName;
            td.path       = str.C_Str();
            td.colorSpace = cs;
            model.textures.push_back(std::move(td));
            indices.push_back((int)model.textures.size() - 1);
        }
        return indices;
    }

    // CPU-only: decode an image file into pixels. Empty pixels => decode failed (upload uses fallback).
    static TextureData decodeTexture(const char *path, const string &directory)
    {
        TextureData td;
        string filename = directory + '/' + string(path);
        int width, height, nrComponents;
        unsigned char *data = stbi_load(filename.c_str(), &width, &height, &nrComponents, 0);
        if (data)
        {
            td.width = width; td.height = height; td.channels = nrComponents;
            td.pixels.assign(data, data + (size_t)width * height * nrComponents);
            stbi_image_free(data);
        }
        else
        {
            std::cout << "Texture failed to load at path: " << filename << std::endl;
            // leave td.pixels empty -> uploadTexture substitutes the magenta fallback
        }
        return td;
    }

    // GL upload of one decoded texture (render thread). Mirrors the old TextureFromFile GL half:
    // grayscale swizzle for 1/2-channel images, mipmaps + repeat, and a magenta 1x1 fallback.
    static unsigned int uploadTexture(const TextureData &td)
    {
        unsigned int textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        if (!td.pixels.empty())
        {
            GLenum format = GL_RGB;
            if (td.channels == 1) {
                format = GL_RED;
                GLint swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_ONE };
                glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
            } else if (td.channels == 2) {
                format = GL_RG;
                GLint swizzle[4] = { GL_RED, GL_RED, GL_RED, GL_GREEN };
                glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
            } else if (td.channels == 4) {
                format = GL_RGBA;
            }

            // Internal format == data format for linear (data) textures; sRGB color maps (base color,
            // emissive) must be stored sRGB so the GPU linearizes on sample. sRGB only applies to 3/4-
            // channel color maps; 1/2-channel maps are always data (roughness/AO/...) and stay linear.
            GLint internalFormat = (GLint)format;
            if (td.colorSpace == ColorSpace::sRGB && td.channels >= 3)
                internalFormat = (td.channels == 4) ? GL_SRGB8_ALPHA8 : GL_SRGB8;

            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, td.width, td.height, 0, format, GL_UNSIGNED_BYTE, td.pixels.data());
            glGenerateMipmap(GL_TEXTURE_2D);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        else
        {
            // Magenta 1x1 fallback so missing textures are visually obvious.
            const unsigned char magenta[4] = { 255, 0, 255, 255 };
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, magenta);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }

        return textureID;
    }
};
#endif