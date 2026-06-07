#pragma once
#include <memory>
#include <string>
#include <iostream>
#include "Core/Components/GameObject.h"
#include "Core/Components/Scene.h"
#include "Core/Engine/GameObjectLoader.h"
#include "Core/Engine/LoadContext.h"

// Loads a scene file and returns its root GameObject.
//
// A scene file IS a GameObject descriptor that carries a Scene component, e.g.:
//   {
//     "name": "Level1",
//     "components": [
//       { "type": "Transform", "position": [0,0,0] },
//       { "type": "Scene", "objects": [ "assets/gameObjects/player.json", ... ] }
//     ]
//   }
// So loading reuses GameObjectLoader; the Scene component builds the child objects
// during its deserialize. This wrapper exists to name the intent and to validate
// that the root actually has a Scene — and is the future home for multi-scene /
// streaming / active-scene logic.
namespace SceneLoader {

    // Build (but do NOT start) the scene root. Caller calls root->start(), which
    // starts the Scene and, in turn, every object it contains. Returns nullptr on
    // I/O / parse error or if the root has no Scene component.
    inline std::unique_ptr<GameObject> fromFile(const std::string& path, LoadContext& ctx) {
        auto root = GameObjectLoader::fromFile(path, ctx);
        if (!root) return nullptr;
        if (!root->getComponent<Scene>()) {
            std::cerr << "[SceneLoader] " << path << " has no Scene component\n";
            return nullptr;
        }
        return root;
    }

    // Convenience: the Scene component of a loaded root (nullptr if root is null).
    inline Scene* sceneOf(GameObject* root) {
        return root ? root->getComponent<Scene>() : nullptr;
    }
}
