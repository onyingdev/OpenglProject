#pragma once
#include "../Core/Component.h"
#include "../Render/model.h"
#include "../Render/shader_s.h"
#include "TransformComponent3D.h"
#include "../../vendor/glm/glm/glm.hpp"
#include "../../vendor/glm/glm/gtc/matrix_transform.hpp"
#include <memory>
#include <string>
#include <utility>

// Attach to a GameObject to give it a renderable mesh model.
class MeshComponent : public Component {
    TransformComponent* tf = nullptr;
public:
    enum class RenderMode { Solid, Wireframe, Hidden };

    std::shared_ptr<Model> ModelRef;
    RenderMode Mode = RenderMode::Solid;

    MeshComponent() = default;
    explicit MeshComponent(std::shared_ptr<Model> model) : ModelRef(std::move(model)) {}
    // Convenience: load a model from disk. For multiple instances of the same model,
    // prefer constructing one shared_ptr<Model> externally and passing it in to avoid duplicate GPU uploads.
    explicit MeshComponent(const std::string& path) : ModelRef(std::make_shared<Model>(path)) {}

    void start() override {
        tf = owner->getComponent<TransformComponent>();
        if (!tf) throw MissingComponentException("MeshComponent", "TransformComponent");
    }

    void draw(Shader& shader) {
        if (Mode == RenderMode::Hidden || !ModelRef) return;

        // Rotation order matches TransformComponent::forward(): yaw(Y), pitch(X), roll(Z).
        glm::mat4 m(1.0f);
        m = glm::translate(m, tf->Position);
        m = glm::rotate(m, glm::radians(tf->RotationEuler.x), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(tf->RotationEuler.y), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(tf->RotationEuler.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, tf->Scale);
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
