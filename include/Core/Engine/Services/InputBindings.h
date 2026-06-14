#pragma once
#include <string>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <cctype>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "Core/Engine/input_manager.h"   // InputManager + GLFW key codes

// Loads action -> key bindings from JSON so controls are data, not hardcoded calls.
// File shape (a flat action:key map, optionally nested under "actions"):
//   { "actions": { "Quit": "Escape", "MoveForward": "Up", "RotateLeft": "N" } }
// Key names are human-readable (case-insensitive): single letters/digits, arrow
// keys, Escape/Space/Enter/Tab, modifiers, F1-F12. Unknown names are skipped with
// a warning so one bad entry doesn't drop the rest.
namespace InputBindings {
    using json = nlohmann::json;

    // Translate a key name to its GLFW code, or GLFW_KEY_UNKNOWN (-1) if unrecognized.
    inline int keyFromName(const std::string& raw) {
        std::string n = raw;
        for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (n.size() == 1) {                     // letters and digits map directly
            char c = n[0];
            if (c >= 'A' && c <= 'Z') return GLFW_KEY_A + (c - 'A');
            if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
        }

        static const std::unordered_map<std::string, int> table = {
            {"ESCAPE", GLFW_KEY_ESCAPE}, {"ESC", GLFW_KEY_ESCAPE},
            {"SPACE", GLFW_KEY_SPACE},
            {"ENTER", GLFW_KEY_ENTER}, {"RETURN", GLFW_KEY_ENTER},
            {"TAB", GLFW_KEY_TAB}, {"BACKSPACE", GLFW_KEY_BACKSPACE},
            {"UP", GLFW_KEY_UP}, {"DOWN", GLFW_KEY_DOWN},
            {"LEFT", GLFW_KEY_LEFT}, {"RIGHT", GLFW_KEY_RIGHT},
            {"LEFTSHIFT", GLFW_KEY_LEFT_SHIFT},   {"RIGHTSHIFT", GLFW_KEY_RIGHT_SHIFT},
            {"LEFTCONTROL", GLFW_KEY_LEFT_CONTROL}, {"LEFTCTRL", GLFW_KEY_LEFT_CONTROL},
            {"RIGHTCONTROL", GLFW_KEY_RIGHT_CONTROL}, {"RIGHTCTRL", GLFW_KEY_RIGHT_CONTROL},
            {"LEFTALT", GLFW_KEY_LEFT_ALT}, {"RIGHTALT", GLFW_KEY_RIGHT_ALT},
            {"F1", GLFW_KEY_F1}, {"F2", GLFW_KEY_F2}, {"F3", GLFW_KEY_F3},
            {"F4", GLFW_KEY_F4}, {"F5", GLFW_KEY_F5}, {"F6", GLFW_KEY_F6},
            {"F7", GLFW_KEY_F7}, {"F8", GLFW_KEY_F8}, {"F9", GLFW_KEY_F9},
            {"F10", GLFW_KEY_F10}, {"F11", GLFW_KEY_F11}, {"F12", GLFW_KEY_F12},
        };
        auto it = table.find(n);
        return it == table.end() ? GLFW_KEY_UNKNOWN : it->second;
    }

    // Read the bindings file and apply every action onto `input`. Existing bindings
    // are cleared first so removed/renamed actions don't linger — but only after a
    // successful parse, so a transient read error (e.g. the file caught mid-save)
    // leaves the current controls intact. Returns false on I/O / parse error.
    inline bool loadInto(InputManager& input, const std::string& path) {
        std::ifstream f(path);
        if (!f) {
            std::cerr << "[InputBindings] cannot open " << path << "\n";
            return false;
        }
        json j;
        try {
            f >> j;
        } catch (const std::exception& e) {
            std::cerr << "[InputBindings] JSON parse error in " << path << ": " << e.what() << "\n";
            return false;
        }

        // Allow either { "actions": { ... } } or a bare { ... } map.
        const json& actions = (j.contains("actions") && j["actions"].is_object()) ? j["actions"] : j;
        if (!actions.is_object()) {
            std::cerr << "[InputBindings] expected an object of action:key pairs in " << path << "\n";
            return false;
        }

        input.ClearActions();   // parse succeeded — safe to replace the binding set (also resets buffers)

        // Global default buffer window (ms -> seconds). Applied to any action without its own "buffer".
        const double defaultMs = j.value("bufferDefaultMs", 120.0);
        input.SetDefaultBufferTime(defaultMs / 1000.0);

        for (auto it = actions.begin(); it != actions.end(); ++it) {
            if (it.key() == "bufferDefaultMs") continue;   // a config knob, not an action (bare-map form)

            // Each action is either "Key" (string, default window) or { "key": "Key", "buffer": ms }.
            std::string keyName;
            double bufferMs = -1.0;   // <0 -> use the default window
            if (it.value().is_string()) {
                keyName = it.value().get<std::string>();
            } else if (it.value().is_object()) {
                const json& o = it.value();
                keyName = o.value("key", std::string{});
                if (o.contains("buffer") && o["buffer"].is_number())
                    bufferMs = o["buffer"].get<double>();
            } else {
                std::cerr << "[InputBindings] action \"" << it.key()
                          << "\" must map to a key name or { key, buffer } object\n";
                continue;
            }

            if (keyName.empty()) {
                std::cerr << "[InputBindings] action \"" << it.key() << "\" has no key\n";
                continue;
            }
            const int key = keyFromName(keyName);
            if (key == GLFW_KEY_UNKNOWN) {
                std::cerr << "[InputBindings] unknown key \"" << keyName
                          << "\" for action \"" << it.key() << "\"\n";
                continue;
            }
            input.BindAction(it.key(), key);
            if (bufferMs >= 0.0) input.SetBufferTime(it.key(), bufferMs / 1000.0);
        }
        return true;
    }

    // Auto hot-reload helper. Construct with the bindings path, then call poll() once
    // per frame: when the file's modification time changes, it re-applies the file so
    // edits take effect live, without restarting. Cheap — a single stat() per frame.
    class Watcher {
        std::string m_path;
        std::filesystem::file_time_type m_stamp{};
        bool m_have = false;

        bool stamp(std::filesystem::file_time_type& out) const {
            std::error_code ec;
            out = std::filesystem::last_write_time(m_path, ec);
            return !ec;
        }

    public:
        explicit Watcher(std::string path) : m_path(std::move(path)) {
            m_have = stamp(m_stamp);   // seed with the current time so first poll() is a no-op
        }

        // Returns true if the file changed and was reloaded this call.
        bool poll(InputManager& input) {
            std::filesystem::file_time_type now;
            if (!stamp(now)) return false;                 // file missing/unreadable right now
            if (m_have && now == m_stamp) return false;    // unchanged

            m_stamp = now;
            m_have  = true;
            if (loadInto(input, m_path)) {
                std::cout << "[InputBindings] reloaded " << m_path << "\n";
                return true;
            }
            return false;
        }
    };
}
