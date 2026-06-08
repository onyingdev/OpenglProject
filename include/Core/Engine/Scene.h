#pragma once
#include "Core/Components/GameObject.h"
#include "Core/Components/TransformComponent3D.h"   // Transform (placement data)
#include "Components/Rendering/MeshComponent.h"       // draw() reads each object's mesh
#include "Render/API/IRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

class SceneManager;   // back-pointer only; full type pulled in by code that crosses scenes

// A Scene owns a set of GameObjects (keyed by name) and runs their start/update/draw.
// It is a plain container — NOT a component — owned directly (by main today, by a
// SceneManager in Phase 2). It has its own `transform`, applied as an offset to every
// object at draw time, so the whole scene can be placed/moved as a unit.
//
// Objects can reach each other through the scene: every object's OwningScene points
// here (set in addObject), so a script does
//     owner->scene()->getObject("Door")->getComponent<DoorScript>()
// or, by type, owner->scene()->findComponent<DoorScript>().
class Scene {
    std::unordered_map<std::string, std::unique_ptr<GameObject>> Objects;

public:
    std::string Name = "Scene";
    Transform   transform;   // scene-wide placement; offsets all objects in draw()

    // The SceneManager that owns this scene (set on load). Lets one scene reach another
    // through the manager: owner->scene()->manager()->getScene("HUD")->getObject("X").
    SceneManager* Manager = nullptr;
    SceneManager* manager() const { return Manager; }

    // --- object management ---

    // Take ownership of obj (keyed by its Name) and link it back to this scene. On a
    // name clash the existing object is replaced (and freed) with a warning.
    GameObject* addObject(std::unique_ptr<GameObject> obj) {
        if (!obj) return nullptr;
        const std::string name = obj->Name;
        if (Objects.count(name))
            std::cerr << "[Scene] duplicate object name \"" << name << "\" — replacing\n";
        obj->OwningScene = this;
        GameObject* raw = obj.get();
        Objects[name] = std::move(obj);
        return raw;
    }

    GameObject* getObject(const std::string& name) {
        auto it = Objects.find(name);
        return it == Objects.end() ? nullptr : it->second.get();
    }

    bool removeObject(const std::string& name) { return Objects.erase(name) > 0; }

    const std::unordered_map<std::string, std::unique_ptr<GameObject>>& objects() const { return Objects; }
    std::size_t count() const { return Objects.size(); }

    // Free every object (and its GPU resources). Call while the GL context is alive.
    void unload() { Objects.clear(); }

    // --- cross-object queries (the interaction API) ---

    // First object in the scene that has a T, or nullptr. Handy for singletons
    // (the player, a GameState object).
    template <class T>
    T* findComponent() {
        for (auto& [name, obj] : Objects)
            if (auto* c = obj->getComponent<T>()) return c;
        return nullptr;
    }

    // Every T across the scene (e.g. all enemies, all pickups).
    template <class T>
    std::vector<T*> findComponents() {
        std::vector<T*> out;
        for (auto& [name, obj] : Objects)
            if (auto* c = obj->getComponent<T>()) out.push_back(c);
        return out;
    }

    // --- lifecycle ---

    void start() {
        for (auto& [name, obj] : Objects) obj->start();
    }

    void update(float dt) {
        for (auto& [name, obj] : Objects) obj->update(dt);
    }

    // Submit every child's mesh, pre-multiplied by the scene transform so the whole
    // scene moves as one. Objects without a (visible) mesh are skipped.
    void draw(IRenderer& renderer) {
        const glm::mat4 sm = sceneMatrix();
        for (auto& [name, obj] : Objects) {
            auto* mc = obj->getComponent<MeshComponent>();
            if (!mc) continue;
            RenderItem item = mc->renderItem();
            if (!item.model) continue;
            item.modelMatrix = sm * item.modelMatrix;
            renderer.submit(item);
        }
    }

private:
    // Scene world transform. Rotation order matches MeshComponent::worldMatrix:
    // yaw(Y), pitch(X), roll(Z).
    glm::mat4 sceneMatrix() const {
        glm::mat4 m(1.0f);
        m = glm::translate(m, transform.Position);
        m = glm::rotate(m, glm::radians(transform.RotationEuler.x), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(transform.RotationEuler.y), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(transform.RotationEuler.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, transform.Scale);
        return m;
    }
};
