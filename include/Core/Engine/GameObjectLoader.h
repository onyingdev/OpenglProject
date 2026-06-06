#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "Core/Components/GameObject.h"
#include "Core/Engine/LoadContext.h"

// Builds GameObjects from JSON of the form:
//   { "name": "Player", "components": [ { "type": "Transform", ... }, ... ] }
// Component construction goes through the ComponentRegistry, so this loader does not
// depend on any concrete component type. Reusable by a future SceneLoader, which
// calls fromJson() once per object.
namespace GameObjectLoader {
    using json = nlohmann::json;

    // Build (but do NOT start) a GameObject from a parsed JSON object. start() is
    // deferred to the caller so a whole scene can be constructed before cross-object
    // references resolve and component lifecycles begin. ctx is non-const because
    // deserialization may register deferred work on it (e.g. reference links).
    std::unique_ptr<GameObject> fromJson(const json& j, LoadContext& ctx);

    // Read + parse a .json file, then fromJson(). Returns nullptr on I/O or parse error.
    std::unique_ptr<GameObject> fromFile(const std::string& path, LoadContext& ctx);
}
