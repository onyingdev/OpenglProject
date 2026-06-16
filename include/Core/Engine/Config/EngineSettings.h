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

// How the window responds to manual resizing:
//   Free       - resize to any size (desktop default)
//   Locked     - not resizable at all (typical "game" behavior) recommended, + fullscreen.
//   KeepAspect - resizable, but GLFW holds the configured aspect ratio so the rendered
//                image never gets stretched into an odd shape (no camera misalignment) TODO: fix internal game size not changing.
enum class ResizeMode { Free, Locked, KeepAspect };

// Frame-profiler toggles (consumed by Profiler at startup). Off by default — flip in render.json to
// print per-stage CPU/GPU timings without a rebuild, mirroring render.debugColliders.
struct ProfilerSettings {
    bool cpu         = false;   // chrono per-stage CPU/submission timing
    bool gpu         = false;   // GL_TIMESTAMP GPU timing
    bool detail      = false;   // also record the finer renderer subsections
    int  reportEvery = 60;      // frames per averaged print

    // Physics step self-profiling (RigidBodyComponent3D statics), grouped under "profiler.physics".
    // Defaults match the in-code defaults so behavior is unchanged when the block is absent.
    bool physTimings = true;    // [phys2] detect/solve/total line
    bool physSolver  = false;   // [phys2-solver]/[phys2-detect] island + detection breakdown
    bool physPrint   = false;   // per-body awake-state dump
};

// Platform/window configuration (consumed by Window).
struct WindowSettings {
    int         width  = 1920;
    int         height = 1080;
    std::string title  = "OpenGL";
    WindowMode  mode   = WindowMode::Windowed;
    ResizeMode  resize = ResizeMode::KeepAspect;   // games usually lock or aspect-lock
    bool        vsync  = false;
    int         glMajor = 3, glMinor = 3;   // GL context version (OpenGL backend only)
};

// All engine/graphics defaults in one place. Authored as JSON under app/Engine and
// loaded at startup; a player settings file can override it via a swappable path.
//
//   { "backend": "opengl",
//     "window": { "width":1920, "height":1080, "title":"OpenGL", "mode":"windowed",
//                 "resize":"keepAspect", "vsync":false, "glMajor":3, "glMinor":3 },
//     "render": { "internalWidth":1920, "internalHeight":1080,
//                 "antialiasing":"msaa", "msaaSamples":2 } }
struct EngineSettings {
    RenderBackend   backend = RenderBackend::OpenGL;
    WindowSettings  window;
    RenderSettings  render;    // internal resolution + AA (consumed by IRenderer)
    ProfilerSettings profiler; // per-stage frame timing toggles (consumed by Profiler)

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
            if (w.contains("mode"))   s.window.mode   = parseMode(w["mode"].get<std::string>(),     s.window.mode);
            if (w.contains("resize")) s.window.resize = parseResize(w["resize"].get<std::string>(), s.window.resize);
        }

        if (j.contains("render") && j["render"].is_object()) {
            const auto& r = j["render"];
            s.render.renderWidth    = r.value("internalWidth",   s.render.renderWidth);
            s.render.renderHeight   = r.value("internalHeight",  s.render.renderHeight);
            s.render.msaaSamples    = r.value("msaaSamples",     s.render.msaaSamples);
            s.render.debugColliders = r.value("debugColliders",  s.render.debugColliders);
            s.render.gamma          = r.value("gamma",           s.render.gamma);
            s.render.exposure       = r.value("exposure",        s.render.exposure);
            if (r.contains("antialiasing")) s.render.aa = parseAA(r["antialiasing"].get<std::string>(), s.render.aa);
            if (r.contains("tonemap")) s.render.tonemap = parseTonemap(r["tonemap"].get<std::string>(), s.render.tonemap);

            if (r.contains("skybox") && r["skybox"].is_object()) {
                const auto& sb = r["skybox"];
                s.render.skybox.enabled        = sb.value("enabled",        s.render.skybox.enabled);
                s.render.skybox.drawBackground = sb.value("drawBackground", s.render.skybox.drawBackground);
                if (sb.contains("faces") && sb["faces"].is_array() && sb["faces"].size() == 6) {
                    for (int i = 0; i < 6; ++i)
                        s.render.skybox.faces[i] = sb["faces"][i].get<std::string>();
                }
            }
        }

        if (j.contains("profiler") && j["profiler"].is_object()) {
            const auto& p = j["profiler"];
            s.profiler.cpu         = p.value("cpu",         s.profiler.cpu);
            s.profiler.gpu         = p.value("gpu",         s.profiler.gpu);
            s.profiler.detail      = p.value("detail",      s.profiler.detail);
            s.profiler.reportEvery = p.value("reportEvery", s.profiler.reportEvery);

            if (p.contains("physics") && p["physics"].is_object()) {
                const auto& ph = p["physics"];
                s.profiler.physTimings = ph.value("timings", s.profiler.physTimings);
                s.profiler.physSolver  = ph.value("solver",  s.profiler.physSolver);
                s.profiler.physPrint   = ph.value("print",   s.profiler.physPrint);
            }
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
    static ResizeMode parseResize(std::string v, ResizeMode fallback) {
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        if (v == "free")       return ResizeMode::Free;
        if (v == "locked")     return ResizeMode::Locked;
        if (v == "keepaspect") return ResizeMode::KeepAspect;
        return fallback;
    }
    static AntiAliasing parseAA(std::string v, AntiAliasing fallback) {
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        if (v == "none") return AntiAliasing::None;
        if (v == "msaa") return AntiAliasing::MSAA;
        if (v == "fxaa") return AntiAliasing::FXAA;
        return fallback;
    }
    static TonemapMode parseTonemap(std::string v, TonemapMode fallback) {
        for (char& c : v) c = (char)std::tolower((unsigned char)c);
        if (v == "none")     return TonemapMode::None;
        if (v == "reinhard") return TonemapMode::Reinhard;
        if (v == "aces")     return TonemapMode::ACES;
        return fallback;
    }
};
