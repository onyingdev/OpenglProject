#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <cctype>
#include <nlohmann/json.hpp>
#include "Render/API/RenderSettings.h"

// Which graphics backend to build. Drives BOTH the renderer factory and the window
// creation path (GL needs a context; Vulkan/other need GLFW_NO_API + a surface).
enum class RenderBackend { OpenGL, Vulkan };

// How the window is presented. Borderless/Fullscreen are placeholders until wired.
enum class WindowMode { Windowed, Fullscreen, Borderless };

// Platform/window configuration (consumed by Window).
struct WindowSettings {
    int         width  = 1920;
    int         height = 1080;
    std::string title  = "OpenGL";
    WindowMode  mode   = WindowMode::Windowed;
    bool        vsync  = false;
    int         glMajor = 3, glMinor = 3;   // GL context version (OpenGL backend only)
};

// All engine/graphics defaults in one place. Authored as JSON under app/Engine and
// loaded at startup; a player settings file can override it via a swappable path.
//
//   { "backend": "opengl",
//     "window": { "width":1920, "height":1080, "title":"OpenGL", "mode":"windowed",
//                 "vsync":false, "glMajor":3, "glMinor":3 },
//     "render": { "internalWidth":1920, "internalHeight":1080,
//                 "antialiasing":"msaa", "msaaSamples":2 } }
struct EngineSettings {
    RenderBackend  backend = RenderBackend::OpenGL;
    WindowSettings window;
    RenderSettings render;   // internal resolution + AA (consumed by IRenderer)

    // Load defaults from `path`; missing keys keep the built-in defaults above. If
    // `overridePath` is given and exists, it is overlaid on top (player settings win).
    static EngineSettings load(const std::string& path, const std::string& overridePath = "") {
        EngineSettings s;
        applyFile(s, path);
        if (!overridePath.empty()) applyFile(s, overridePath);
        s.render.vsync = s.window.vsync;   // keep the single source of truth in sync
        return s;
    }

private:
    static void applyFile(EngineSettings& s, const std::string& path) {
        std::ifstream f(path);
        if (!f) { std::cerr << "[EngineSettings] cannot open " << path << " (using defaults)\n"; return; }
        nlohmann::json j;
        try { f >> j; }
        catch (const std::exception& e) {
            std::cerr << "[EngineSettings] parse error in " << path << ": " << e.what() << " (using defaults)\n";
            return;
        }
        applyJson(s, j);
    }

    static void applyJson(EngineSettings& s, const nlohmann::json& j) {
        if (j.contains("backend")) s.backend = parseBackend(j["backend"].get<std::string>(), s.backend);

        if (j.contains("window") && j["window"].is_object()) {
            const auto& w = j["window"];
            s.window.width   = w.value("width",   s.window.width);
            s.window.height  = w.value("height",  s.window.height);
            s.window.title   = w.value("title",   s.window.title);
            s.window.vsync   = w.value("vsync",   s.window.vsync);
            s.window.glMajor = w.value("glMajor", s.window.glMajor);
            s.window.glMinor = w.value("glMinor", s.window.glMinor);
            if (w.contains("mode")) s.window.mode = parseMode(w["mode"].get<std::string>(), s.window.mode);
        }

        if (j.contains("render") && j["render"].is_object()) {
            const auto& r = j["render"];
            s.render.renderWidth    = r.value("internalWidth",   s.render.renderWidth);
            s.render.renderHeight   = r.value("internalHeight",  s.render.renderHeight);
            s.render.msaaSamples    = r.value("msaaSamples",     s.render.msaaSamples);
            s.render.debugColliders = r.value("debugColliders",  s.render.debugColliders);
            if (r.contains("antialiasing")) s.render.aa = parseAA(r["antialiasing"].get<std::string>(), s.render.aa);
        }
    }

    static RenderBackend parseBackend(std::string v, RenderBackend fallback) {
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        if (v == "opengl" || v == "gl") return RenderBackend::OpenGL;
        if (v == "vulkan" || v == "vk") return RenderBackend::Vulkan;
        return fallback;
    }
    static WindowMode parseMode(std::string v, WindowMode fallback) {
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        if (v == "windowed")   return WindowMode::Windowed;
        if (v == "fullscreen") return WindowMode::Fullscreen;
        if (v == "borderless") return WindowMode::Borderless;
        return fallback;
    }
    static AntiAliasing parseAA(std::string v, AntiAliasing fallback) {
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        if (v == "none") return AntiAliasing::None;
        if (v == "msaa") return AntiAliasing::MSAA;
        return fallback;
    }
};
