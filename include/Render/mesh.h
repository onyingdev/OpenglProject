#ifndef MESH_H
#define MESH_H

#include <glad/glad.h>// holds all OpenGL type declarations

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Render/API/IShader.h"
#include "Render/API/IMesh.h"
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
    unsigned int VAO = 0;

    // constructor — full (synchronous) upload. Move the vectors in (they're already owned rvalues from
    // the loader); copying them here was a wasted multi-MB memcpy on the upload frame.
    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures)
    {
        this->vertices = std::move(vertices);
        this->indices  = std::move(indices);
        this->textures = std::move(textures);

        // now that we have all the required data, set the vertex buffers and its attribute pointers.
        setupMesh();
    }

    // Streamed constructor — allocate VBO/EBO sized for the geometry but DON'T upload yet; the data is
    // fed later via uploadGeometryStep() across frames. `textures` stays empty until the owning Model
    // finishes uploading its texture pool and assigns them. Used by the async streamed-upload path.
    Mesh(size_t vertexCount, size_t indexCount, MeshStreamTag)
    {
        setupMeshStreamed(vertexCount, indexCount);
    }

    // Move-only: a Mesh owns its VAO/VBO/EBO. Copying would duplicate the handles and
    // then double-delete them; instead we transfer ownership and zero the source so its
    // destructor frees nothing. (Required for vector<Mesh> in Model to relocate safely.)
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& o) noexcept
        : vertices(std::move(o.vertices)), indices(std::move(o.indices)),
          textures(std::move(o.textures)), VAO(o.VAO), VBO(o.VBO), EBO(o.EBO), instanceVBO(o.instanceVBO),
          m_indexCount(o.m_indexCount), m_vboBytes(o.m_vboBytes), m_eboBytes(o.m_eboBytes),
          m_vboCursor(o.m_vboCursor), m_eboCursor(o.m_eboCursor)
    {
        o.VAO = o.VBO = o.EBO = o.instanceVBO = 0;
    }
    Mesh& operator=(Mesh&& o) noexcept
    {
        if (this != &o) {
            release();
            vertices = std::move(o.vertices);
            indices  = std::move(o.indices);
            textures = std::move(o.textures);
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
    void DrawInstanced(IShader &shader, const glm::mat4* mats, int count)
    {
        if (count <= 0) return;
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

        bindTextures(shader);
        glDrawElementsInstanced(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0, count);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }

private:
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
        // Per-family counters give each texture its sampler suffix (the N in texture_diffuseN). The PBR
        // families (metallic_roughness/occlusion/emissive) are bound here too so they're ready for the
        // future PBR shader; setInt on a sampler the current shader doesn't declare is a cached no-op.
        unsigned int diffuseNr  = 1;
        unsigned int specularNr = 1;
        unsigned int normalNr   = 1;
        unsigned int heightNr   = 1;
        unsigned int mrNr        = 1;
        unsigned int occlusionNr = 1;
        unsigned int emissiveNr  = 1;
        for(unsigned int i = 0; i < textures.size(); i++)
        {
            glActiveTexture(GL_TEXTURE0 + i); // active proper texture unit before binding
            // retrieve texture number (the N in diffuse_textureN)
            string number;
            string name = textures[i].type;
            if(name == "texture_diffuse")
                number = std::to_string(diffuseNr++);
            else if(name == "texture_specular")
                number = std::to_string(specularNr++); // transfer unsigned int to string
            else if(name == "texture_normal")
                number = std::to_string(normalNr++); // transfer unsigned int to string
            else if(name == "texture_height")
                number = std::to_string(heightNr++); // transfer unsigned int to string
            else if(name == "texture_metallic_roughness")
                number = std::to_string(mrNr++);
            else if(name == "texture_occlusion")
                number = std::to_string(occlusionNr++);
            else if(name == "texture_emissive")
                number = std::to_string(emissiveNr++);

            // now set the sampler to the correct texture unit (via the interface,
            // no direct access to a backend-specific program handle)
            shader.setInt(name + number, i);
            // and finally bind the texture
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }
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
