#include "Core/Engine/GameObjectLoader.h"
#include "Core/Engine/ComponentRegistry.h"
#include "Core/Components/Component.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

// Note: this file intentionally includes NO concrete component headers. Component
// types are resolved by name through the ComponentRegistry; each component owns its
// own deserialization. Adding a new component never touches this file.

namespace GameObjectLoader {

std::unique_ptr<GameObject> fromJson(const json& j, LoadContext& ctx) {
    auto obj = std::make_unique<GameObject>();
    obj->Name = j.value("name", std::string{"GameObject"});

    if (j.contains("components") && j["components"].is_array()) {
        for (const auto& c : j["components"]) {
            if (!c.contains("type")) {
                std::cerr << "[GameObjectLoader] component with no \"type\" on '" << obj->Name << "'\n";
                continue;
            }
            const std::string type = c["type"].get<std::string>();

            Component* comp = ComponentRegistry::instance().create(type, *obj);
            if (!comp) {
                std::cerr << "[GameObjectLoader] unknown component type \"" << type
                          << "\" (not registered) on '" << obj->Name << "'\n";
                continue;
            }
            try {
                comp->deserialize(c, ctx);
            } catch (const std::exception& e) {
                std::cerr << "[GameObjectLoader] deserialize failed for \"" << type
                          << "\" on '" << obj->Name << "': " << e.what() << "\n";
            }
        }
    }
    return obj;
}

std::unique_ptr<GameObject> fromFile(const std::string& path, LoadContext& ctx) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "[GameObjectLoader] cannot open " << path << "\n";
        return nullptr;
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "[GameObjectLoader] JSON parse error in " << path << ": " << e.what() << "\n";
        return nullptr;
    }
    return fromJson(j, ctx);
}

} // namespace GameObjectLoader
