#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Helpers/JsonHelpers.h"
#include "Render/API/IModel.h"
#include "Render/API/RenderTypes.h"
#include "Render/model.h"          // concrete GL Model, used only at construction
#include "Render/ModelCache.h"     // share one GPU upload per model path (spawns, scene duplicates)
#include "Render/CubemapCache.h"   // share one cube-map upload per face set (reflective objects)
#include "Render/Assets/AssetManager.h" // async (threaded) loading: request() + ModelHandle
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

    // Shading is data, not a per-frame caller override: "lit" uses scene lighting,
    // "unlit" draws in a flat UnlitColor (e.g. a lamp/marker). Defaults preserve the
    // old behavior (everything Lit) so meshes without these keys render unchanged.
    MaterialKind Material = MaterialKind::Lit;
    glm::vec3    UnlitColor{1.0f};

    // Environment cube map this object reflects when Material == Reflective. 0 means "use the scene
    // skybox" (set in JSON via "material":"reflective" with no faces). A non-zero id comes from
    // CubemapCache (shared per face set). GL cube texture id; the cache owns its lifetime.
    unsigned int EnvCubemap = 0;

    MeshComponent() = default;
    explicit MeshComponent(std::shared_ptr<IModel> model) : ModelRef(std::move(model)) {}
    // Convenience: load a FRESH, un-shared model from disk (the direct-load path). For shared
    // instances prefer the cache — deserialize() below goes through ModelCache so repeated loads of
    // the same path reuse one GPU upload; this ctor is kept for callers that deliberately want a
    // separate Model instance. flipUVs: set true for models authored with the LearnOpenGL UV convention.
    explicit MeshComponent(const std::string& path, bool flipUVs = false)
        : ModelRef(std::make_shared<Model>(path, false, flipUVs)) {}

    // JSON: { "type": "Mesh", "model": "assets/.../foo.obj", "flipUVs": false,
    //         "material": "lit"|"unlit"|"reflective", "color": [r,g,b],
    //         "cubemap": [ "+x.jpg","-x.jpg","+y.jpg","-y.jpg","+z.jpg","-z.jpg" ] }
    // A "cubemap" array implies a reflective material; "material":"reflective" with no cubemap
    // reflects the scene skybox instead.
    void deserialize(const nlohmann::json& j, LoadContext&) override {
        const std::string model = j.value("model", std::string{});
        const bool flip  = j.value("flipUVs", false);
        // "async": parse on a worker thread and pop the mesh in once uploaded — no load-time hitch.
        // Default false keeps the old blocking load, so existing scenes are unchanged. Prefer true for
        // heavy models / runtime spawns; the object simply renders nothing until the upload completes.
        const bool async = j.value("async", false);
        if (!model.empty()) {
            if (async) m_pending = AssetManager::instance().request(model, false, flip);
            else       ModelRef  = ModelCache::get(model, false, flip);   // shared upload per path
        }

        const std::string mat = j.value("material", std::string{"lit"});
        Material   = (mat == "unlit")      ? MaterialKind::Unlit
                   : (mat == "reflective") ? MaterialKind::Reflective
                                           : MaterialKind::Lit;
        UnlitColor = jsonVec3(j.value("color", nlohmann::json{}), UnlitColor);

        // Optional per-object environment cube map (6 faces, GL order +X,-X,+Y,-Y,+Z,-Z). Loading is
        // synchronous + cached; assigning one forces the reflective material so it actually shows.
        if (j.contains("cubemap") && j["cubemap"].is_array() && j["cubemap"].size() == 6) {
            std::array<std::string, 6> faces{};
            for (int i = 0; i < 6; ++i) faces[i] = j["cubemap"][i].get<std::string>();
            EnvCubemap = CubemapCache::get(faces);
            Material   = MaterialKind::Reflective;
        }
    }

    // Promote a finished async load into the live model. Cheap no-op for synchronously-loaded meshes
    // (m_pending is null) and after the first promotion. renderItem() draws nothing until then.
    void update(float) override {
        if (!ModelRef && m_pending && m_pending->ready) {
            ModelRef = m_pending->model;
            m_pending.reset();
        }
    }

    // World transform for this mesh. Rotation comes from the quaternion authority
    // (orientation()): for Euler-authored/animated objects it's derived from RotationEuler in
    // the same yaw(Y)/pitch(X)/roll(Z) order (bit-identical to the old three rotates); for a
    // physics body it's the integrated quaternion the body wrote to the Transform.
    glm::mat4 worldMatrix() const {
        glm::mat4 m(1.0f);
        m = glm::translate(m, position());
        m = m * glm::mat4_cast(orientation());
        m = glm::scale(m, scale());
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
        item.material    = Material;
        item.unlitColor  = UnlitColor;
        item.envCubemap  = EnvCubemap;   // 0 -> renderer falls back to the scene skybox
        return item;
    }

private:
    // In-flight async load: set by deserialize when "async": true, cleared once update() promotes it
    // into ModelRef. Null for synchronously-loaded meshes.
    std::shared_ptr<ModelHandle> m_pending;
};

REGISTER_COMPONENT(MeshComponent, "Mesh")
