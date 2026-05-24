#pragma once
#include <memory>
#include <vector>
#include <typeindex>
#include <unordered_map>
#include "Component.h"

class GameObject {
    std::vector<std::unique_ptr<Component>> Components;
    std::unordered_map<std::type_index, Component*> Lookup;
    bool Started = false;

public:
    std::string Name = "Unnamed GameObject";
    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {
        static_assert(std::is_base_of_v<Component, T>);
        auto c = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = c.get();
        raw->owner = this;
        Lookup[std::type_index(typeid(T))] = raw;
        Components.push_back(std::move(c));
        if (Started) raw->start();   // hot-add after start
        return raw;
    }

    template<typename T>
    T* getComponent() {
        auto it = Lookup.find(std::type_index(typeid(T)));
        return it == Lookup.end() ? nullptr : static_cast<T*>(it->second);
    }

    void start() {
        Started = true;
        for (auto& c : Components) {
            try {
                c->start();
            }
            catch (const std::exception& e) {
                std::cerr << "[GameObject \"" << Name << "\"] start failed: " << e.what() << '\n';
                throw; // TODO: modify this so its throws more cleanly
            }
        }
    }

    void update(float dt) {
        for (auto& c : Components) c->update(dt);
    }
};
