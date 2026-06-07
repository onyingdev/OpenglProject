// A Scene contains game objects and manages their execution loop (start/update/draw).
// Scenes can store different levels, menus, etc. and be switched between at runtime.
//
// A Scene is a SpatialComponent3D: it has its own transform, so it can be placed
// anywhere and ALL of its objects translate/rotate/scale along with it (the scene
// transform is applied as an offset to every child at draw time). Because it is a
// registerable component, a Scene can also be nested inside another object as a
// sub-scene / prefab instance.
//
// Ownership: the Scene owns its GameObjects (unique_ptr), keyed by name. unload()
// clears them, running their destructors — which release GPU resources (meshes,
// textures) while the GL context is still alive.

#pragma once
#include "Core/Components/SpatialComponent3D.h"
#include "Core/Engine/ComponentRegistry.h"
#include "Core/Engine/GameObjectLoader.h"   // build child objects from JSON (decl only)
#include "Components/Rendering/MeshComponent.h"
#include "Render/API/IRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <memory>
#include <string>
#include <iostream>

class Scene : public SpatialComponent3D {
    std::unordered_map<std::string, std::unique_ptr<GameObject>> Objects;

public:
    // --- object management ---

    // Take ownership of obj, keyed by its Name. On a name clash the existing object
    // is replaced (and freed) with a warning — names are expected unique per scene.
    GameObject* addObject(std::unique_ptr<GameObject> obj) {
        if (!obj) return nullptr;
        const std::string name = obj->Name;
        if (Objects.count(name))
            std::cerr << "[Scene] duplicate object name \"" << name << "\" — replacing\n";
        GameObject* raw = obj.get();
        Objects[name] = std::move(obj);
        return raw;
    }

    // Borrow an object by name (Scene retains ownership). nullptr if absent.
    GameObject* getObject(const std::string& name) {
        auto it = Objects.find(name);
        return it == Objects.end() ? nullptr : it->second.get();
    }

    bool removeObject(const std::string& name) { return Objects.erase(name) > 0; }

    const std::unordered_map<std::string, std::unique_ptr<GameObject>>& objects() const { return Objects; }
    std::size_t count() const { return Objects.size(); }

    // Free every object (and its GPU resources). Call while the GL context is alive.
    void unload() { Objects.clear(); }

    // --- JSON: { "type": "Scene", "objects": [ "path/to/obj.json", { inline obj }, ... ] } ---
    // Entries are either a path string (loaded via GameObjectLoader::fromFile) or an
    // inline object definition. Children are built but NOT started here; start() is
    // deferred to the Scene's own start() so the whole scene exists first.
    void deserialize(const nlohmann::json& j, LoadContext& ctx) override {
        if (!j.contains("objects") || !j["objects"].is_array()) return;
        for (const auto& entry : j["objects"]) {
            std::unique_ptr<GameObject> obj;
            if (entry.is_string())      obj = GameObjectLoader::fromFile(entry.get<std::string>(), ctx);
            else if (entry.is_object()) obj = GameObjectLoader::fromJson(entry, ctx);
            else { std::cerr << "[Scene] object entry must be a path string or object\n"; continue; }

            if (obj) addObject(std::move(obj));
            else     std::cerr << "[Scene] failed to load an object entry\n";
        }
    }

    void start() override {
        SpatialComponent3D::start();
        // Track the owner's TransformComponent3D live, so moving the scene root at
        // runtime moves the whole scene (children follow via the offset in draw()).
        useOwnerPosition();
        useOwnerRotation();
        useOwnerScale();
        for (auto& [name, obj] : Objects) obj->start();
    }

    void update(float dt) override {
        for (auto& [name, obj] : Objects) obj->update(dt);
    }

    // Submit every child's mesh to the renderer, pre-multiplied by the scene transform
    // so the whole scene moves as one. Objects without a (visible) mesh are skipped.
    void draw(IRenderer& renderer) {
        const glm::mat4 sm = sceneMatrix();
        for (auto& [name, obj] : Objects) {
            auto* mc = obj->getComponent<MeshComponent>();
            if (!mc) continue;
            RenderItem item = mc->renderItem();
            if (!item.model) continue;             // hidden / no model
            item.modelMatrix = sm * item.modelMatrix;
            renderer.submit(item);
        }
    }

private:
    // The scene's own world transform. Rotation order matches MeshComponent::worldMatrix:
    // yaw(Y), pitch(X), roll(Z).
    glm::mat4 sceneMatrix() const {
        const glm::vec3& p = position();
        const glm::vec3& r = rotation();
        const glm::vec3& s = scale();
        glm::mat4 m(1.0f);
        m = glm::translate(m, p);
        m = glm::rotate(m, glm::radians(r.x), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(r.y), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(r.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, s);
        return m;
    }
};

REGISTER_COMPONENT(Scene, "Scene")
