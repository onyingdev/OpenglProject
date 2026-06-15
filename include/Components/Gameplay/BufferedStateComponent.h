#pragma once
#include "Core/Components/Component.h"
#include "Core/Engine/Services/ComponentRegistry.h"
#include "Core/Engine/Services/LoadContext.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <iostream>

// Named, time-limited gameplay states that automatically revert to a default after a TTL. Data-driven:
// states are declared in JSON; scripts set them and they expire on their own. Examples: a character is
// "invulnerable" for 0.5s after a hit, or "buff" = "strength" for 5s after a power-up.
//
// Timers are dt-based (no graphics/GLFW dependency) so behavior is identical at any frame rate. State
// values are strings, which covers both flags ("true"/"false") and multi-value states (buff types,
// stances); SetFlag/IsFlag are the boolean convenience for the common on/off case.
//
// JSON: { "type": "BufferedState",
//         "states": [ { "name": "invulnerable", "default": "false", "ttl": 0.5 },
//                     { "name": "buff",         "default": "none",  "ttl": 5.0 } ] }
class BufferedStateComponent : public Component {
public:
    void deserialize(const nlohmann::json& j, LoadContext&) override {
        if (!j.contains("states") || !j["states"].is_array()) return;
        for (const auto& sj : j["states"]) {
            const std::string name = sj.value("name", std::string{});
            if (name.empty()) continue;
            const std::string def = jsonToString(sj.contains("default") ? sj["default"] : nlohmann::json{});
            const double ttl = sj.value("ttl", 0.0);
            m_states[name] = State{ def, def, ttl, 0.0 };   // starts at default, no active timer
        }
    }

    // Tick active timers; revert a state to its default when its timer elapses.
    void update(float dt) override {
        for (auto& kv : m_states) {
            State& s = kv.second;
            if (s.remaining > 0.0) {
                s.remaining -= dt;
                if (s.remaining <= 0.0) { s.value = s.def; s.remaining = 0.0; }
            }
        }
    }

    // Set a state's value. ttlOverride > 0 uses that window; <= 0 uses the state's configured ttl (and a
    // configured ttl of 0 means "no auto-revert" — stays until Reset/Set). Setting back to the default
    // clears the timer.
    void Set(const std::string& name, const std::string& value, double ttlOverride) {
        auto it = m_states.find(name);
        if (it == m_states.end()) {
            std::cerr << "[BufferedState] Set on undefined state \"" << name
                      << "\" — declare it in JSON or call Define()\n";
            return;
        }
        State& s = it->second;
        s.value = value;
        if (value == s.def) s.remaining = 0.0;                          // already default: no countdown
        else                s.remaining = (ttlOverride > 0.0) ? ttlOverride : s.ttl;
    }
    void Set(const std::string& name, const std::string& value) { Set(name, value, -1.0); }

    // Force a state back to its default immediately.
    void Reset(const std::string& name) {
        auto it = m_states.find(name);
        if (it == m_states.end()) return;
        it->second.value = it->second.def;
        it->second.remaining = 0.0;
    }

    std::string Get(const std::string& name) const {
        auto it = m_states.find(name);
        return it != m_states.end() ? it->second.value : std::string{};
    }
    bool Is(const std::string& name, const std::string& value) const { return Get(name) == value; }

    // Boolean convenience: a state is "on" while its value is "true". ttl < 0 uses the configured window.
    void SetFlag(const std::string& name, double ttl = -1.0) { Set(name, "true", ttl); }
    bool IsFlag (const std::string& name) const              { return Is(name, "true"); }

    // Declare a state at runtime (alternative to the JSON `states[]`). Resets it to `def`.
    void Define(const std::string& name, const std::string& def, double ttl) {
        m_states[name] = State{ def, def, ttl, 0.0 };
    }

private:
    struct State {
        std::string value;       // current
        std::string def;         // value reverted to when the timer elapses
        double      ttl = 0.0;   // configured revert window (seconds); 0 = no auto-revert
        double      remaining = 0.0;  // active countdown; 0 = none running
    };
    std::unordered_map<std::string, State> m_states;

    // Stringify a JSON scalar so authors can write "default" as a string, bool, or number.
    static std::string jsonToString(const nlohmann::json& v) {
        if (v.is_string())          return v.get<std::string>();
        if (v.is_boolean())         return v.get<bool>() ? "true" : "false";
        if (v.is_number_integer())  return std::to_string(v.get<long long>());
        if (v.is_number())          return std::to_string(v.get<double>());
        return std::string{};
    }
};

REGISTER_COMPONENT(BufferedStateComponent, "BufferedState")
