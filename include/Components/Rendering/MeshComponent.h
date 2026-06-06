#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Render/model.h"
#include "Render/shader_s.h"
#include <memory>
#include <string>
#include <utility>

// Attach to a GameObject to give it a renderable mesh model.
class MeshComponent : public SpatialComponent3D {
public:
    enum class RenderMode { Solid, Wireframe, Hidden };

    std::shared_ptr<Model> ModelRef;
    RenderMode Mode = RenderMode::Solid;

    MeshComponent() = default;
    explicit MeshComponent(std::shared_ptr<Model> model) : ModelRef(std::move(model)) {}
    // Convenience: load a model from disk. For multiple instances of the same model,
    // prefer constructing one shared_ptr<Model> externally and passing it in to avoid duplicate GPU uploads.
    // flipUVs: forward to Model — set true for models authored with the LearnOpenGL UV convention.
    explicit MeshComponent(const std::string& path, bool flipUVs = false)
        : ModelRef(std::make_shared<Model>(path, false, flipUVs)) {}

    void draw(Shader& shader) {
        if (Mode == RenderMode::Hidden || !ModelRef) return;

        const glm::vec3& pos = position();
        const glm::vec3& rot = rotation();
        const glm::vec3& scl = scale();

        // Rotation order matches TransformComponent::forward(): yaw(Y), pitch(X), roll(Z).
        glm::mat4 m(1.0f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(rot.x), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rot.y), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(rot.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, scl);
        shader.setMat4("model", m);

        if (Mode == RenderMode::Wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            ModelRef->Draw(shader);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        } else {
            ModelRef->Draw(shader);
        }
    }
};
