#pragma once
#include "Core/Engine/Helpers/exceptions.h"
#include <nlohmann/json_fwd.hpp>   // lightweight: declares nlohmann::json without the full library

class GameObject;
struct LoadContext;

class Component {

public:
    virtual ~Component() = default;
    virtual void start()   {}              // called once after all components attached
    virtual void update(float dt) {}       // called every frame

    // Populate this component's fields from its JSON descriptor. Default is a no-op
    // (for components with no serialized data, e.g. CharacterComponent). ctx carries
    // runtime services (InputManager, ...) that are injected, not stored in the file.
    virtual void deserialize(const nlohmann::json& /*j*/, LoadContext& /*ctx*/) {}

    GameObject* owner = nullptr;
};