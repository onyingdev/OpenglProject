#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

// Application / bootstrap config — distinct from Engine settings (graphics) and Config
// (input). It answers "which world boots": the root scene manifest path. Kept out of
// main so it isn't hardcoded, and watched at runtime so the world can be repointed on
// the fly (see SceneManager::switchManifest). The seed for a future "open project".
//
//   { "rootScene": "assets/scenes/scenes.json" }
struct AppConfig {
    std::string rootScene = "assets/scenes/root/scenes.json";

    static AppConfig load(const std::string& path) {
        AppConfig c;
        std::ifstream f(path);
        if (!f) { std::cerr << "[AppConfig] cannot open " << path << " (using defaults)\n"; return c; }
        nlohmann::json j;
        try { f >> j; }
        catch (const std::exception& e) {
            std::cerr << "[AppConfig] parse error in " << path << ": " << e.what() << " (using defaults)\n";
            return c;
        }
        c.rootScene = j.value("rootScene", c.rootScene);
        return c;
    }
};
