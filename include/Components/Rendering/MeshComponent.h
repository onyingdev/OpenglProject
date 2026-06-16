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
#include <array>
#include <fstream>

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

    // Per-object IBL environment cube map (GL cube texture id from CubemapCache, shared per face set;
    // the cache owns its lifetime). The object stays Lit and uses this environment's baked IBL probe
    // instead of the scene skybox. 0 = use the scene-default skybox probe.
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
    //         "material": "lit"|"unlit", "color": [r,g,b],
    //         "renderMode": "solid"|"wireframe"|"hidden",
    //         "cubemap": "assets/cubemaps/foo.json",       // or an inline 6-element face array
    //         "doubleSided": true }                        // force both faces lit (asset omitted the flag)
    // A "cubemap" makes the object a NORMAL lit object that reflects/receives ambient from that
    // environment's IBL probe (shared per cube map) instead of the scene skybox. "doubleSided" forces
    // every mesh double-sided — use it when a glTF leaves a glass/thin material single-sided.
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

        Material   = (j.value("material", std::string{"lit"}) == "unlit")
                         ? MaterialKind::Unlit : MaterialKind::Lit;
        UnlitColor = jsonVec3(j.value("color", nlohmann::json{}), UnlitColor);

        // Render mode: solid (default) / wireframe (lines) / hidden (skipped in renderItem()).
        const std::string rm = j.value("renderMode", std::string{"solid"});
        Mode = (rm == "wireframe") ? RenderMode::Wireframe
             : (rm == "hidden")    ? RenderMode::Hidden
                                   : RenderMode::Solid;

        // Optional per-object environment cube map -> this object's IBL probe (it stays Lit). "cubemap"
        // is a path to a cubemap.json ({ "faces": [6 paths] }) or an inline 6-element face array (GL
        // order +X,-X,+Y,-Y,+Z,-Z). The env cube is shared via CubemapCache and its IBL bake is shared
        // per env cube (baked lazily on first draw). Missing/invalid -> 0 -> scene-default IBL.
        if (j.contains("cubemap")) {
            std::array<std::string, 6> faces{};
            if (resolveCubemapFaces(j["cubemap"], faces))
                EnvCubemap = CubemapCache::get(faces);
        }

        // Force double-sided when the asset omitted the glTF flag (e.g. single-sided glass). Applied now
        // for sync loads; the async path re-applies on promotion in update(). Mutates the shared model.
        m_forceDoubleSided = j.value("doubleSided", false);
        if (m_forceDoubleSided && ModelRef) ModelRef->setDoubleSided(true);
    }

    // Promote a finished async load into the live model. Cheap no-op for synchronously-loaded meshes
    // (m_pending is null) and after the first promotion. renderItem() draws nothing until then.
    void update(float) override {
        if (!ModelRef && m_pending && m_pending->ready) {
            ModelRef = m_pending->model;
            m_pending.reset();
            if (m_forceDoubleSided && ModelRef) ModelRef->setDoubleSided(true);
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
    // Resolve a "cubemap" JSON node to 6 face paths (GL order +X,-X,+Y,-Y,+Z,-Z). Accepts either an
    // inline 6-element array, or a string path to a cubemap.json of the form { "faces": [ 6 paths ] }.
    // Returns false on any malformed/missing input (caller then leaves EnvCubemap = 0).
    static bool resolveCubemapFaces(const nlohmann::json& node, std::array<std::string, 6>& out) {
        const nlohmann::json* arr = nullptr;
        nlohmann::json fileJson;   // must outlive `arr` when loaded from a file
        if (node.is_string()) {
            std::ifstream f(node.get<std::string>());
            if (!f) return false;
            try { f >> fileJson; } catch (const std::exception&) { return false; }
            if (!fileJson.contains("faces") || !fileJson["faces"].is_array()) return false;
            arr = &fileJson["faces"];
        } else if (node.is_array()) {
            arr = &node;
        } else {
            return false;
        }
        if (arr->size() != 6) return false;
        for (int i = 0; i < 6; ++i) {
            if (!(*arr)[i].is_string()) return false;
            out[i] = (*arr)[i].get<std::string>();
        }
        return true;
    }

    // In-flight async load: set by deserialize when "async": true, cleared once update() promotes it
    // into ModelRef. Null for synchronously-loaded meshes.
    std::shared_ptr<ModelHandle> m_pending;

    // "doubleSided": true in JSON — force every mesh of the model double-sided after it loads (covers
    // assets whose exporter omitted the glTF flag). Applied in deserialize (sync) / update (async).
    bool m_forceDoubleSided = false;
};

REGISTER_COMPONENT(MeshComponent, "Mesh")
