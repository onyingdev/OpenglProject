#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "Core/Engine/Scene.h"
#include "Core/Engine/Services/SceneLoader.h"
#include "Core/Engine/Services/GameObjectLoader.h"   // loose/root objects from "objects" in the manifest
#include "Core/Engine/Services/LoadContext.h"
#include "Render/API/IRenderer.h"
#include "Render/API/RenderTypes.h"
#include "Components/Rendering/PointLightComponent.h"
#include "Components/Rendering/ConeLightComponent.h"
#include "Components/Rendering/DirectionalLightComponent.h"

// The single root manager: there is exactly one. It owns every loaded Scene, drives
// their update/draw, gathers their lights, and is the intermediary scenes go through
// to reach each other (Scene::manager()->getScene(...)).
//
// What to load is listed in a manifest JSON — `scenes` (loaded/unloaded as a set) and
// `objects` (loose/root objects like the camera, loaded once and persistent):
//   { "scenes":  [ { "name": "Level1", "path": "assets/scenes/level1.json", "active": true } ],
//     "objects": [ "assets/gameObjects/camera.json" ] }
// The manifest and every scene file it references are watched: poll() reloads the scenes
// when any of them changes on disk, and reload() does the same on demand (so a key/button
// can force it). Loose objects persist across those reloads. Seed of a future editor.
//
// Lifetime note: because the manager is a singleton, its scenes would otherwise be freed
// at static-destruction time — AFTER the GL context is gone. Always call unloadAll()
// before tearing down the window so Mesh/Model destructors run while GL is still alive.
class SceneManager {
public:
    static SceneManager& instance() {
        static SceneManager mgr;   // Meyers singleton: the one and only root manager
        return mgr;
    }

    // Load every scene named in the manifest; remembers the path for hot-reload.
    void loadManifest(const std::string& path, LoadContext& ctx) {
        m_manifestPath = path;
        m_watch.clear();
        watch(path);

        std::ifstream f(path);
        if (!f) { std::cerr << "[SceneManager] cannot open manifest " << path << "\n"; return; }
        nlohmann::json j;
        try { f >> j; }
        catch (const std::exception& e) {
            std::cerr << "[SceneManager] manifest parse error in " << path << ": " << e.what() << "\n";
            return;
        }

        if (!j.contains("scenes") || !j["scenes"].is_array()) {
            std::cerr << "[SceneManager] manifest " << path << " has no \"scenes\" array\n";
            return;
        }
        for (const auto& s : j["scenes"]) {
            const std::string name  = s.value("name", std::string{});
            const std::string spath = s.value("path", std::string{});
            const bool        active = s.value("active", true);
            if (spath.empty()) { std::cerr << "[SceneManager] scene entry with no \"path\"\n"; continue; }

            auto scene = SceneLoader::fromFile(spath, ctx);
            if (!scene) { std::cerr << "[SceneManager] failed to load scene " << spath << "\n"; continue; }
            if (!name.empty()) scene->Name = name;
            scene->Manager = this;
            scene->start();              // all objects exist -> cross-object refs resolve
            watch(spath);
            // Capture the key BEFORE the move: in `m_scenes[scene->Name] = Entry{std::move(scene),...}`
            // C++17 sequences the right operand first, so scene would already be null when the
            // subscript reads scene->Name. Take the name now, then move.
            const std::string key = scene->Name;
            m_scenes[key] = Entry{ std::move(scene), active };
        }

        // Loose/root objects (camera, global managers): listed under "objects" as paths
        // or inline definitions. Loaded ONCE — they persist across scene hot-reloads, so
        // we don't recreate them (or reset their state) when only scenes change.
        if (!m_looseLoaded && j.contains("objects") && j["objects"].is_array()) {
            for (const auto& entry : j["objects"]) {
                std::unique_ptr<GameObject> obj;
                if (entry.is_string())      obj = GameObjectLoader::fromFile(entry.get<std::string>(), ctx);
                else if (entry.is_object()) obj = GameObjectLoader::fromJson(entry, ctx);
                else { std::cerr << "[SceneManager] root object entry must be a path string or object\n"; continue; }

                if (obj) addObject(std::move(obj));   // owns + starts it (after scenes, so refs resolve)
                else     std::cerr << "[SceneManager] failed to load a root object entry\n";
            }
            m_looseLoaded = true;
        }
    }

    // --- loose ("root") objects: GameObjects the manager owns directly, NOT part of
    // any scene (e.g. the camera, global managers). Their OwningScene is null; reach
    // the manager from their scripts via SceneManager::instance(). They survive scene
    // hot-reloads (only unloadAll, at shutdown, frees them). ---

    // Take ownership of a loose object, start it, and return a borrowed pointer that
    // stays valid for the program's life (loose objects aren't freed on scene reload).
    GameObject* addObject(std::unique_ptr<GameObject> obj) {
        if (!obj) return nullptr;
        const std::string key = obj->Name;
        if (m_loose.count(key))
            std::cerr << "[SceneManager] duplicate root object \"" << key << "\" — replacing\n";
        obj->OwningScene = nullptr;     // loose: belongs to the root, not a scene
        GameObject* raw = obj.get();
        obj->start();
        m_loose[key] = std::move(obj);
        return raw;
    }

    // Free only the scenes (used by reload). Loose objects are left untouched so the
    // camera and other globals persist across a scene hot-reload.
    void unloadScenes() {
        for (auto& [name, e] : m_scenes) e.scene->unload();
        m_scenes.clear();
    }

    // Free EVERYTHING (scenes + loose objects). Call at shutdown while the GL context
    // is still alive, before the window is torn down.
    void unloadAll() {
        unloadScenes();
        m_loose.clear();
    }

    // Manual hot-reload (bind to a key/button): rebuild the scenes from the manifest.
    // Loose objects (camera, globals) are kept — only the scenes are swapped.
    void reload(LoadContext& ctx) {
        if (m_manifestPath.empty()) return;
        std::cout << "[SceneManager] reloading scenes from " << m_manifestPath << "\n";
        unloadScenes();
        loadManifest(m_manifestPath, ctx);
    }

    // Swap to a DIFFERENT manifest at runtime (open another world/project). Unlike
    // reload() — which keeps loose objects so the camera persists — this frees EVERYTHING
    // and reloads loose objects from the new manifest too. Callers that cached a loose
    // object pointer (e.g. the camera) must re-fetch it after this.
    void switchManifest(const std::string& path, LoadContext& ctx) {
        std::cout << "[SceneManager] switching manifest -> " << path << "\n";
        unloadAll();
        m_looseLoaded = false;            // the new manifest's loose objects must load
        loadManifest(path, ctx);
    }

    // Automatic hot-reload: if the manifest or any scene file changed on disk, reload.
    // Returns true if a reload happened. Cheap — one stat per watched file per frame.
    bool poll(LoadContext& ctx) {
        bool changed = false;
        for (const auto& [path, stamp] : m_watch) {
            std::error_code ec;
            const auto now = std::filesystem::last_write_time(path, ec);
            if (!ec && now != stamp) { changed = true; break; }
        }
        if (changed) { reload(ctx); return true; }
        return false;
    }

    // --- access / queries ---

    Scene* getScene(const std::string& name) {
        // std::cout << "=== DEBUG: m_scenes (" << m_scenes.size() << " entries) ===\n";
        // for (const auto& [key, entry] : m_scenes) {
        //     std::cout << "Map Key:  \"" << key << "\"\n";
        //     std::cout << "  Active: " << (entry.active ? "true" : "false") << "\n";
            
        //     if (entry.scene) {
        //         std::cout << "  Scene->Name: \"" << entry.scene->Name << "\"\n";
        //     } else {
        //         std::cout << "  Scene->Name: [NULL POINTER]\n";
        //     }
        // }
        // std::cout << "======================================\n";
        auto it = m_scenes.find(name);
        return it == m_scenes.end() ? nullptr : it->second.scene.get();
    }

    // Object finders are split because names are only unique WITHIN a scene (or within
    // the loose set) — the same name can exist in several scenes, so the caller must say
    // where to look:
    //   findObjectInScenes  - first match scanning every scene (ambiguous if duplicated)
    //   findObjectInScene   - look in one named scene only (unambiguous)
    //   findObjectInRoot    - look among the loose/root objects only

    // First object with this name across ALL scenes. Convenient but ambiguous when the
    // name is reused across scenes — prefer findObjectInScene when you know the scene.
    GameObject* findObjectInScenes(const std::string& name) {
        for (auto& [sname, e] : m_scenes)
            if (auto* o = e.scene->getObject(name)) return o;
        return nullptr;
    }

    // Object by name within a specific scene (unambiguous). Null if the scene or the
    // object is missing.
    GameObject* findObjectInScene(const std::string& sceneName, const std::string& name) {
        auto it = m_scenes.find(sceneName);
        return it == m_scenes.end() ? nullptr : it->second.scene->getObject(name);
    }

    // Loose/root object by name (the camera, global managers, ...).
    GameObject* findObjectInRoot(const std::string& name) {
        auto it = m_loose.find(name);
        return it == m_loose.end() ? nullptr : it->second.get();
    }

    // First component of type T across all loaded scenes.
    template <class T>
    T* findComponent() {
        for (auto& [sname, e] : m_scenes)
            if (auto* c = e.scene->findComponent<T>()) return c;
        return nullptr;
    }

    // --- per-frame drive (active scenes only) ---

    //TODO: update loose objects only, update scenes only, update specific scenes and loose object.
    //unload specific scene or object.

    void update(float dt) {
        for (auto& [name, e] : m_scenes)
            if (e.active) e.scene->update(dt);
        // Loose objects update after the scenes, so a camera/follow sees this frame's
        // already-updated scene objects.
        for (auto& [name, obj] : m_loose) obj->update(dt);
    }

    void draw(IRenderer& renderer) {
        for (auto& [name, e] : m_scenes)
            if (e.active) e.scene->draw(renderer);
    }

    // Gather the sun + point + cone lights from every active scene into `lights`.
    void collectLights(LightSet& lights) {
        for (auto& [name, e] : m_scenes) {
            if (!e.active) continue;
            for (auto& [oname, obj] : e.scene->objects()) appendObjectLights(*obj, lights);
        }
        // Loose/root objects can carry lights too (e.g. a flashlight on the camera).
        for (auto& [name, obj] : m_loose) appendObjectLights(*obj, lights);
    }

private:
    struct Entry {
        std::unique_ptr<Scene> scene;
        bool                   active = true;
    };

    std::unordered_map<std::string, Entry> m_scenes;        // by scene name
    std::unordered_map<std::string, std::unique_ptr<GameObject>> m_loose;  // root objects, by name
    bool                                   m_looseLoaded = false;  // root objects load once, then persist
    std::string                            m_manifestPath;
    std::unordered_map<std::string, std::filesystem::file_time_type> m_watch;

    void watch(const std::string& path) {
        std::error_code ec;
        m_watch[path] = std::filesystem::last_write_time(path, ec);  // epoch on error; first poll reloads
    }

    // Append any light components on `obj` to `lights` (shared by scenes + loose objects).
    static void appendObjectLights(GameObject& obj, LightSet& lights) {
        if (auto* dl = obj.getComponent<DirectionalLightComponent>()) lights.dir = dl->toDirLight();
        if (auto* pl = obj.getComponent<PointLightComponent>())       lights.points.push_back(pl->toPointLight());
        if (auto* cl = obj.getComponent<ConeLightComponent>())        lights.spots.push_back(cl->toSpotLight());
    }

    SceneManager() = default;
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
};
