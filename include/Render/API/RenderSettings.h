#pragma once
#include <array>
#include <string>
#include <glm/glm.hpp>

// Runtime-tunable rendering configuration. Changing the render resolution or the
// anti-aliasing mode rebuilds the renderer's offscreen targets on the fly (see
// IRenderer::applySettings); the rest of the engine is unaffected.
enum class AntiAliasing {
    None,   // render straight to a single-sample target, no AA
    MSAA,   // hardware multisample + resolve, using msaaSamples
    FXAA,   // single-sample render + post-process edge filter in the present pass
    // Future post-process modes: SMAA, TAA, ...
};

// HDR-to-LDR operator applied in the present pass (the scene is lit in linear HDR in the
// RGBA16F buffer). Maps unbounded radiance into [0,1] before the gamma encode so bright
// specular / additive lights / emissive no longer hard-clip. Integer values are uploaded
// directly to the present shaders' `tonemap` uniform — keep in sync with the GLSL branches.
enum class TonemapMode {
    None     = 0,   // straight clamp (legacy behaviour)
    Reinhard = 1,   // x / (1+x); simple, slightly desaturated highlights
    ACES     = 2,   // Narkowicz ACES filmic approximation (default)
};

// Environment cubemap drawn behind the opaque scene (and, later, the source for IBL).
// faces are in OpenGL cube-face order: +X, -X, +Y, -Y, +Z, -Z
// (right, left, top, bottom, front, back). Loaded once at init; absent/empty -> disabled.
struct SkyboxSettings {
    bool                          enabled = false;
    bool                          drawBackground = true;   // false = hide the background cube but keep IBL
    float                         diffuse  = 1.0f;         // IBL irradiance (env-as-light) multiplier
    float                         specular = 1.0f;         // IBL reflection multiplier
    std::array<std::string, 6>    faces{};
};

struct RenderSettings {
    int          renderWidth  = 1920;             // internal/offscreen render resolution
    int          renderHeight = 1080;             // (independent of the window size)
    AntiAliasing aa           = AntiAliasing::MSAA;
    int          msaaSamples  = 2;                // only used when aa == MSAA (2, 4, 8, ...)
    bool         vsync        = false;            // applied by the Window/context, not the renderer
    bool         debugColliders = false;          // draw collider/hurtbox wireframes (initial state; toggled at runtime)
    float        gamma        = 2.2f;             // present-pass output gamma exponent (sRGB ~2.2)
    TonemapMode  tonemap      = TonemapMode::ACES; // HDR->LDR operator before the gamma encode
    float        exposure     = 1.0f;             // linear exposure multiplier applied before tonemapping
    SkyboxSettings skybox;                        // environment cubemap (background; IBL source later)
    glm::vec4 backgroundColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
};
