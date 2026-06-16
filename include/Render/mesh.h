#ifndef MESH_H
#define MESH_H

#include <glad/glad.h>// holds all OpenGL type declarations

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Render/API/IShader.h"
#include "Render/API/IMesh.h"
#include "Render/API/IModel.h"         // MaterialPass (Opaque/Transparent) for the per-mesh draw split
#include "Render/Assets/ModelData.h"   // Vertex (+ MAX_BONE_INFLUENCE) — shared with the loader

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
using namespace std;

// GPU-side texture handle: a GL texture id + its sampler family/path. The CPU-side decoded pixels live
// in TextureData (ModelData.h); this is what remains after upload.
struct Texture {
    unsigned int id;
    string type;
    string path;
};

// Tag for the streamed Mesh constructor: allocate the GPU buffers now, upload the geometry later in
// chunks (see Mesh::uploadGeometryStep + AssetManager's streamed upload pump).
struct MeshStreamTag {};

// OpenGL implementation of IMesh. Owns the VAO/VBO/EBO for one mesh.
class Mesh : public IMesh {
public:
    // mesh Data
    vector<Vertex>       vertices;
    vector<unsigned int> indices;
    vector<Texture>      textures;
    glm::vec3            baseColor{1.0f};   // flat material color; set as the "baseColor" uniform at draw
                                            // time and multiplied with the diffuse map (white = no-op)
    // PBR metallic-roughness factors (glTF). Set per-mesh from MeshData after construction; emitted as
    // uniforms in Draw/DrawInstanced. Each multiplies its matching map (or stands alone when absent).
    float                metallic = 1.0f;
    float                roughness = 1.0f;
    glm::vec3            emissiveFactor{0.0f};
    float                occlusionStrength = 1.0f;
    float                normalScale = 1.0f;
    // glTF alpha: baseColorAlpha = baseColorFactor.a; alphaMode 0=OPAQUE/1=MASK/2=BLEND; alphaCutoff for
    // MASK. The renderer draws BLEND meshes in a separate blended pass; MASK discards in the shader.
    float                baseColorAlpha = 1.0f;
    int                  alphaMode = 0;
    float                alphaCutoff = 0.5f;
    bool                 doubleSided = false;   // glTF: light both faces (shader flips back-face normals)
    unsigned int VAO = 0;

    // constructor — full (synchronous) upload. Move the vectors in (they're already owned rvalues from
    // the loader); copying them here was a wasted multi-MB memcpy on the upload frame.
    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures,
         glm::vec3 baseColor = glm::vec3(1.0f))
    {
        this->vertices  = std::move(vertices);
        this->indices   = std::move(indices);
        this->textures  = std::move(textures);
        this->baseColor = baseColor;

        // now that we have all the required data, set the vertex buffers and its attribute pointers.
        setupMesh();
    }

    // Streamed constructor — allocate VBO/EBO sized for the geometry but DON'T upload yet; the data is
    // fed later via uploadGeometryStep() across frames. `textures` stays empty until the owning Model
    // finishes uploading its texture pool and assigns them. Used by the async streamed-upload path.
    Mesh(size_t vertexCount, size_t indexCount, glm::vec3 baseColor, MeshStreamTag)
    {
        this->baseColor = baseColor;
        setupMeshStreamed(vertexCount, indexCount);
    }

    // Move-only: a Mesh owns its VAO/VBO/EBO. Copying would duplicate the handles and
    // then double-delete them; instead we transfer ownership and zero the source so its
    // destructor frees nothing. (Required for vector<Mesh> in Model to relocate safely.)
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& o) noexcept
        : vertices(std::move(o.vertices)), indices(std::move(o.indices)),
          textures(std::move(o.textures)), baseColor(o.baseColor),
          metallic(o.metallic), roughness(o.roughness), emissiveFactor(o.emissiveFactor),
          occlusionStrength(o.occlusionStrength), normalScale(o.normalScale),
          baseColorAlpha(o.baseColorAlpha), alphaMode(o.alphaMode), alphaCutoff(o.alphaCutoff),
          doubleSided(o.doubleSided),
          VAO(o.VAO), VBO(o.VBO), EBO(o.EBO), instanceVBO(o.instanceVBO),
          m_indexCount(o.m_indexCount), m_vboBytes(o.m_vboBytes), m_eboBytes(o.m_eboBytes),
          m_vboCursor(o.m_vboCursor), m_eboCursor(o.m_eboCursor)
    {
        o.VAO = o.VBO = o.EBO = o.instanceVBO = 0;
    }
    Mesh& operator=(Mesh&& o) noexcept
    {
        if (this != &o) {
            release();
            vertices  = std::move(o.vertices);
            indices   = std::move(o.indices);
            textures  = std::move(o.textures);
            baseColor = o.baseColor;
            metallic = o.metallic; roughness = o.roughness; emissiveFactor = o.emissiveFactor;
            occlusionStrength = o.occlusionStrength; normalScale = o.normalScale;
            baseColorAlpha = o.baseColorAlpha; alphaMode = o.alphaMode; alphaCutoff = o.alphaCutoff;
            doubleSided = o.doubleSided;
            VAO = o.VAO; VBO = o.VBO; EBO = o.EBO; instanceVBO = o.instanceVBO;
            m_indexCount = o.m_indexCount;
            m_vboBytes = o.m_vboBytes; m_eboBytes = o.m_eboBytes;
            m_vboCursor = o.m_vboCursor; m_eboCursor = o.m_eboCursor;
            o.VAO = o.VBO = o.EBO = o.instanceVBO = 0;
        }
        return *this;
    }

    // --- streamed geometry upload (async path) ----------------------------------------------------
    // Upload up to `budgetBytes` of geometry from the supplied source pointers (the owning Model's
    // retained ModelData), VBO first then EBO, via glBufferSubData. Returns bytes consumed. Call each
    // frame until geometryDone(). Does NOT touch the mesh VAO binding (safe to call from the pump).
    size_t uploadGeometryStep(const Vertex* vsrc, const unsigned int* isrc, size_t budgetBytes)
    {
        size_t consumed = 0;
        if (m_vboCursor < m_vboBytes) {
            const size_t chunk = std::min(budgetBytes, m_vboBytes - m_vboCursor);
            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)m_vboCursor, (GLsizeiptr)chunk,
                            reinterpret_cast<const unsigned char*>(vsrc) + m_vboCursor);
            m_vboCursor += chunk; consumed += chunk;
            if (consumed >= budgetBytes) return consumed;
        }
        if (m_eboCursor < m_eboBytes) {
            const size_t chunk = std::min(budgetBytes - consumed, m_eboBytes - m_eboCursor);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)m_eboCursor, (GLsizeiptr)chunk,
                            reinterpret_cast<const unsigned char*>(isrc) + m_eboCursor);
            m_eboCursor += chunk; consumed += chunk;
        }
        return consumed;
    }
    bool geometryDone() const { return m_vboCursor >= m_vboBytes && m_eboCursor >= m_eboBytes; }

    // Frees the GPU buffers. Must run while the GL context is alive (scene unload /
    // shutdown both happen before the window is torn down).
    ~Mesh()
    {
        release();
    }

    // render the mesh (single instance, model matrix supplied via the shader's `model` uniform)
    void Draw(IShader &shader) override
    {
        setMaterialUniforms(shader);
        bindTextures(shader);
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);   // restore default texture unit
    }

    // Render `count` copies in ONE call. Each copy's model matrix comes from `mats`, uploaded to a
    // per-instance buffer wired to attribute locations 7-10 (a mat4, divisor 1). The geometry VAO is
    // shared across all instances; only this buffer differs per frame. Pair with an instanced shader
    // that reads the model matrix from that attribute instead of a uniform.
    void DrawInstanced(IShader &shader, const glm::mat4* mats, int count,
                       MaterialPass pass = MaterialPass::Opaque)
    {
        if (count <= 0) return;
        // Per-mesh alpha split: BLEND meshes (alphaMode 2) draw only in the Transparent pass; OPAQUE +
        // MASK meshes draw only in the Opaque pass (MASK then discards in the shader). Skip mismatches.
        const bool isBlend = (alphaMode == 2);
        if (isBlend != (pass == MaterialPass::Transparent)) return;

        glBindVertexArray(VAO);

        if (instanceVBO == 0) {
            // First use: create the instance buffer and wire the mat4 attribute (4 vec4 columns)
            // into THIS VAO once. Divisor 1 = advance per instance, not per vertex.
            glGenBuffers(1, &instanceVBO);
            glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
            for (unsigned int c = 0; c < 4; ++c) {
                glEnableVertexAttribArray(7 + c);
                glVertexAttribPointer(7 + c, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                      (void*)(c * sizeof(glm::vec4)));
                glVertexAttribDivisor(7 + c, 1);
            }
        }

        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * (GLsizeiptr)sizeof(glm::mat4), mats, GL_DYNAMIC_DRAW);

        setMaterialUniforms(shader);
        bindTextures(shader);
        glDrawElementsInstanced(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0, count);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }

private:
    // Per-mesh PBR material factors -> shader uniforms (sampler-name convention handles the maps).
    // Names a non-PBR shader doesn't declare resolve to a cached -1 and are silently ignored.
    void setMaterialUniforms(IShader &shader)
    {
        shader.setVec3 ("baseColor",         baseColor.x, baseColor.y, baseColor.z);
        shader.setFloat("metallicFactor",    metallic);
        shader.setFloat("roughnessFactor",   roughness);
        shader.setVec3 ("emissiveFactor",    emissiveFactor.x, emissiveFactor.y, emissiveFactor.z);
        shader.setFloat("occlusionStrength", occlusionStrength);
        shader.setFloat("normalScale",       normalScale);
        shader.setFloat("baseColorAlpha",    baseColorAlpha);
        shader.setInt  ("alphaMode",         alphaMode);
        shader.setFloat("alphaCutoff",       alphaCutoff);
        shader.setInt  ("doubleSided",       doubleSided ? 1 : 0);
    }

    // render data
    unsigned int VBO = 0, EBO = 0;
    unsigned int instanceVBO = 0;   // per-instance model matrices (lazily created on first DrawInstanced)
    unsigned int m_indexCount = 0;  // element count for draws (decoupled from the `indices` vector)
    // Streamed-upload progress (bytes). Unused/zero for fully-uploaded (sync) meshes.
    size_t m_vboBytes = 0, m_eboBytes = 0;
    size_t m_vboCursor = 0, m_eboCursor = 0;

    // Bind this mesh's textures to sequential units and point the matching samplers at them.
    // Shared by Draw and DrawInstanced (uniform-sampler names are cached in the Shader now).
    void bindTextures(IShader &shader)
    {
        // Sampler families for the PBR set (plus legacy specular/height for OBJ/FBX, ignored by the PBR
        // shader). Track which PBR families got a real map so we can bind a neutral default for the rest.
        unsigned int diffuseNr  = 1, mrNr = 1, normalNr = 1, occlusionNr = 1, emissiveNr = 1;
        unsigned int specularNr = 1, heightNr = 1;
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i); // active proper texture unit before binding
            string number;
            const string& name = textures[i].type;
            if      (name == "texture_diffuse")    number = std::to_string(diffuseNr++);
            else if (name == "texture_mr")         number = std::to_string(mrNr++);
            else if (name == "texture_normal")     number = std::to_string(normalNr++);
            else if (name == "texture_occlusion")  number = std::to_string(occlusionNr++);
            else if (name == "texture_emissive")   number = std::to_string(emissiveNr++);
            else if (name == "texture_specular")   number = std::to_string(specularNr++);
            else if (name == "texture_height")     number = std::to_string(heightNr++);

            shader.setInt(name + number, i);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }

        // Neutral defaults for any missing PBR sampler — both to make the factors authoritative on
        // untextured meshes AND to stop a sampler reading whatever the PREVIOUS mesh left on the unit.
        // white baseColor/mr/occlusion/emissive = factors stand alone; flat (0,0,1) normal = vertex normal.
        unsigned int unit = (unsigned int)textures.size();
        auto bindDefault = [&](unsigned int present, const char* sampler, unsigned int tex) {
            if (present != 1) return;                     // a real map of this family was bound
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, tex);
            shader.setInt(sampler, (int)unit);
            ++unit;
        };
        bindDefault(diffuseNr,   "texture_diffuse1",   defaultTexture(true));
        bindDefault(mrNr,        "texture_mr1",        defaultTexture(true));
        bindDefault(normalNr,    "texture_normal1",    defaultNormalTexture());
        bindDefault(occlusionNr, "texture_occlusion1", defaultTexture(true));
        bindDefault(emissiveNr,  "texture_emissive1",  defaultTexture(true));
    }

    // Process-lifetime 1x1 fallback textures (white / black), created on first use on the GL thread and
    // shared by every mesh. Never freed — 1x1 each, and they must outlive all meshes regardless.
    static unsigned int defaultTexture(bool white)
    {
        static unsigned int whiteTex = 0, blackTex = 0;
        unsigned int& tex = white ? whiteTex : blackTex;
        if (tex == 0) {
            const unsigned char v = white ? 255 : 0;
            const unsigned char px[4] = { v, v, v, 255 };
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        return tex;
    }

    // 1x1 flat tangent-space normal (0,0,1) encoded as RGB (128,128,255): decodes to the geometric
    // normal, so a mesh with no normal map shades as if normal-mapping were off.
    static unsigned int defaultNormalTexture()
    {
        static unsigned int tex = 0;
        if (tex == 0) {
            const unsigned char px[4] = { 128, 128, 255, 255 };
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        return tex;
    }

    void release()
    {
        if (EBO) glDeleteBuffers(1, &EBO);
        if (VBO) glDeleteBuffers(1, &VBO);
        if (instanceVBO) glDeleteBuffers(1, &instanceVBO);
        if (VAO) glDeleteVertexArrays(1, &VAO);
        VAO = VBO = EBO = instanceVBO = 0;
    }

    // initializes all the buffer objects/arrays and uploads the full geometry (synchronous path)
    void setupMesh()
    {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        // load data into vertex buffers. A struct's memory layout is sequential, so a pointer to the
        // first Vertex translates directly to a tightly-packed float/int array.
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        configureAttribs();
        glBindVertexArray(0);
        m_indexCount = static_cast<unsigned int>(indices.size());
    }

    // Streamed path: create the VAO/VBO/EBO and RESERVE storage (glBufferData with null data) sized for
    // the geometry, wire the attribs, but upload nothing yet — uploadGeometryStep() fills it in chunks.
    void setupMeshStreamed(size_t vertexCount, size_t indexCount)
    {
        m_vboBytes = vertexCount * sizeof(Vertex);
        m_eboBytes = indexCount * sizeof(unsigned int);

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_vboBytes, nullptr, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)m_eboBytes, nullptr, GL_STATIC_DRAW);

        configureAttribs();
        glBindVertexArray(0);
        m_indexCount = static_cast<unsigned int>(indexCount);
        m_vboCursor = m_eboCursor = 0;
    }

    // Wire the vertex attribute pointers into the currently-bound VAO (reads from VBO). Shared by the
    // full and streamed setup so the layout stays in one place.
    void configureAttribs()
    {
        glBindBuffer(GL_ARRAY_BUFFER, VBO);   // attribs capture the currently-bound array buffer
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
        glEnableVertexAttribArray(5);
        glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));
    }
};
#endif
