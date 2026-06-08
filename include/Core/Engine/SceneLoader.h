#pragma once
#include <memory>
#include <string>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "Core/Engine/Scene.h"
#include "Core/Engine/GameObjectLoader.h"
#include "Core/Engine/JsonHelpers.h"
#include "Core/Engine/LoadContext.h"

// Builds a Scene from a JSON scene file:
//   {
//     "name": "Level1",
//     "transform": { "position": [0,0,0], "rotation": [...], "scale": [...] },
//     "objects": [ "assets/gameObjects/player.json", { inline object }, ... ]
//   }
// Each entry of "objects" is either a path string (loaded via GameObjectLoader::fromFile)
// or an inline object definition. Objects are built but NOT started — the caller starts
// the whole Scene once it exists, so cross-object references can resolve safely.
// (Phase 2's SceneManager will wrap this and own the loaded scenes.)
namespace SceneLoader {
    using json = nlohmann::json;

    inline std::unique_ptr<Scene> fromJson(const json& j, LoadContext& ctx) {
        auto scene = std::make_unique<Scene>();
        scene->Name = j.value("name", std::string{"Scene"});

        if (j.contains("transform") && j["transform"].is_object()) {
            const json& t = j["transform"];
            scene->transform.Position      = jsonVec3(t.value("position", json{}), scene->transform.Position);
            scene->transform.RotationEuler = jsonVec3(t.value("rotation", json{}), scene->transform.RotationEuler);
            scene->transform.Scale         = jsonVec3(t.value("scale",    json{}), scene->transform.Scale);
        }

        if (j.contains("objects") && j["objects"].is_array()) {
            for (const auto& entry : j["objects"]) {
                std::unique_ptr<GameObject> obj;
                if (entry.is_string())      obj = GameObjectLoader::fromFile(entry.get<std::string>(), ctx);
                else if (entry.is_object()) obj = GameObjectLoader::fromJson(entry, ctx);
                else { std::cerr << "[SceneLoader] object entry must be a path string or object\n"; continue; }

                if (obj) scene->addObject(std::move(obj));
                else     std::cerr << "[SceneLoader] failed to load an object entry in scene '" << scene->Name << "'\n";
            }
        }
        return scene;
    }

    // Read + parse a .json scene file, then fromJson(). Returns nullptr on I/O or parse error.
    inline std::unique_ptr<Scene> fromFile(const std::string& path, LoadContext& ctx) {
        std::ifstream f(path);
        if (!f) {
            std::cerr << "[SceneLoader] cannot open " << path << "\n";
            return nullptr;
        }
        json j;
        try {
            f >> j;
        } catch (const std::exception& e) {
            std::cerr << "[SceneLoader] JSON parse error in " << path << ": " << e.what() << "\n";
            return nullptr;
        }
        return fromJson(j, ctx);
    }
}
