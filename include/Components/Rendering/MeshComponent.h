#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Engine/ComponentRegistry.h"
#include "Render/API/IModel.h"
#include "Render/API/RenderTypes.h"
#include "Render/model.h"          // concrete GL Model, used only at construction
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <utility>

// Attach to a GameObject to give it a renderable mesh model. The component is pure
// data + transform: it produces a RenderItem for the renderer to draw, and never
// touches a graphics API itself.
class MeshComponent : public SpatialComponent3D {
public:
    enum class RenderMode { Solid, Wireframe, Hidden };

    std::shared_ptr<IModel> ModelRef;
    RenderMode Mode = RenderMode::Solid;

    MeshComponent() = default;
    explicit MeshComponent(std::shared_ptr<IModel> model) : ModelRef(std::move(model)) {}
    // Convenience: load a model from disk. For multiple instances of the same model,
    // prefer constructing one shared_ptr<IModel> externally and passing it in to avoid duplicate GPU uploads.
    // flipUVs: forward to Model — set true for models authored with the LearnOpenGL UV convention.
    explicit MeshComponent(const std::string& path, bool flipUVs = false)
        : ModelRef(std::make_shared<Model>(path, false, flipUVs)) {}

    // JSON: { "type": "Mesh", "model": "assets/.../foo.obj", "flipUVs": false }
    void deserialize(const nlohmann::json& j, LoadContext&) override {
        const std::string model = j.value("model", std::string{});
        const bool flip = j.value("flipUVs", false);
        if (!model.empty()) ModelRef = std::make_shared<Model>(model, false, flip);
    }

    // World transform for this mesh. Rotation order matches
    // TransformComponent::forward(): yaw(Y), pitch(X), roll(Z).
    glm::mat4 worldMatrix() const {
        const glm::vec3& pos = position();
        const glm::vec3& rot = rotation();
        const glm::vec3& scl = scale();
        glm::mat4 m(1.0f);
        m = glm::translate(m, pos);
        m = glm::rotate(m, glm::radians(rot.x), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rot.y), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(rot.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, scl);
        return m;
    }

    // Build a renderer submission for this mesh. model is null when hidden or unset,
    // which the renderer treats as a no-op. The caller may override the material and
    // unlitColor (e.g. to draw a lamp as Unlit).
    RenderItem renderItem() const {
        RenderItem item;
        if (Mode == RenderMode::Hidden || !ModelRef) return item;  // item.model == nullptr
        item.model       = ModelRef.get();
        item.modelMatrix = worldMatrix();
        item.wireframe   = (Mode == RenderMode::Wireframe);
        return item;
    }
};

REGISTER_COMPONENT(MeshComponent, "Mesh")
