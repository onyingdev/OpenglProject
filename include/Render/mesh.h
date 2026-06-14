#ifndef MESH_H
#define MESH_H

#include <glad/glad.h>// holds all OpenGL type declarations

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Render/API/IShader.h"
#include "Render/API/IMesh.h"
#include "Render/Assets/ModelData.h"   // Vertex (+ MAX_BONE_INFLUENCE) — shared with the loader

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

// OpenGL implementation of IMesh. Owns the VAO/VBO/EBO for one mesh.
class Mesh : public IMesh {
public:
    // mesh Data
    vector<Vertex>       vertices;
    vector<unsigned int> indices;
    vector<Texture>      textures;
    unsigned int VAO = 0;

    // constructor
    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<Texture> textures)
    {
        this->vertices = vertices;
        this->indices = indices;
        this->textures = textures;

        // now that we have all the required data, set the vertex buffers and its attribute pointers.
        setupMesh();
    }

    // Move-only: a Mesh owns its VAO/VBO/EBO. Copying would duplicate the handles and
    // then double-delete them; instead we transfer ownership and zero the source so its
    // destructor frees nothing. (Required for vector<Mesh> in Model to relocate safely.)
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    Mesh(Mesh&& o) noexcept
        : vertices(std::move(o.vertices)), indices(std::move(o.indices)),
          textures(std::move(o.textures)), VAO(o.VAO), VBO(o.VBO), EBO(o.EBO), instanceVBO(o.instanceVBO)
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
            o.VAO = o.VBO = o.EBO = o.instanceVBO = 0;
        }
        return *this;
    }

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
        glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indices.size()), GL_UNSIGNED_INT, 0);
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
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<unsigned int>(indices.size()),
                                GL_UNSIGNED_INT, 0, count);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
    }

private:
    // render data
    unsigned int VBO = 0, EBO = 0;
    unsigned int instanceVBO = 0;   // per-instance model matrices (lazily created on first DrawInstanced)

    // Bind this mesh's textures to sequential units and point the matching samplers at them.
    // Shared by Draw and DrawInstanced (uniform-sampler names are cached in the Shader now).
    void bindTextures(IShader &shader)
    {
        unsigned int diffuseNr  = 1;
        unsigned int specularNr = 1;
        unsigned int normalNr   = 1;
        unsigned int heightNr   = 1;
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

    // initializes all the buffer objects/arrays
    void setupMesh()
    {
        // create buffers/arrays
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        // load data into vertex buffers
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        // A great thing about structs is that their memory layout is sequential for all its items.
        // The effect is that we can simply pass a pointer to the struct and it translates perfectly to a glm::vec3/2 array which
        // again translates to 3/2 floats which translates to a byte array.
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);  

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

        // set the vertex attribute pointers
        // vertex Positions
        glEnableVertexAttribArray(0);	
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        // vertex normals
        glEnableVertexAttribArray(1);	
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        // vertex texture coords
        glEnableVertexAttribArray(2);	
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        // vertex tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
        // vertex bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
		// ids
		glEnableVertexAttribArray(5);
		glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));

		// weights
		glEnableVertexAttribArray(6);
		glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));
        glBindVertexArray(0);
    }
};
#endif
