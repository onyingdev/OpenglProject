#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include "Core/Components/Component.h"
#include "Core/Components/GameObject.h"

// Maps a JSON "type" name to a factory that constructs that component on a
// GameObject. Components self-register via the REGISTER_COMPONENT macro in their
// own headers, so the loader depends only on this registry — never on concrete
// component types. Adding a new serializable component touches only that component.
class ComponentRegistry {
public:
    using Factory = std::function<Component*(GameObject&)>;

    static ComponentRegistry& instance() {
        static ComponentRegistry registry;   // Meyers singleton: no static-init-order issues
        return registry;
    }

    void add(const std::string& name, Factory factory) {
        m_factories[name] = std::move(factory);
    }

    // Construct the named component on obj (added to and owned by obj). Returns the
    // new component, or nullptr if the name was never registered.
    Component* create(const std::string& name, GameObject& obj) const {
        auto it = m_factories.find(name);
        return (it == m_factories.end()) ? nullptr : it->second(obj);
    }

private:
    std::unordered_map<std::string, Factory> m_factories;
};

// Helper whose constructor registers T under `name` at static-init time.
template <class T>
struct ComponentRegistrar {
    explicit ComponentRegistrar(const std::string& name) {
        ComponentRegistry::instance().add(name, [](GameObject& obj) {
            return static_cast<Component*>(obj.addComponent<T>());
        });
    }
};

// Self-register a component type under its JSON "type" name. Place at namespace
// scope just after the class definition in the component's header. `inline` keeps a
// single registrar shared across all translation units that include the header.
#define REGISTER_COMPONENT(TYPE, NAME) \
    inline const ComponentRegistrar<TYPE> g_register_##TYPE{NAME};
