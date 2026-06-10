#pragma once

// Runtime-tunable rendering configuration. Changing the render resolution or the
// anti-aliasing mode rebuilds the renderer's offscreen targets on the fly (see
// IRenderer::applySettings); the rest of the engine is unaffected.
enum class AntiAliasing {
    None,   // render straight to a single-sample target, no AA
    MSAA,   // hardware multisample + resolve, using msaaSamples
    FXAA,   // single-sample render + post-process edge filter in the present pass
    // Future post-process modes: SMAA, TAA, ...
};

struct RenderSettings {
    int          renderWidth  = 1920;             // internal/offscreen render resolution
    int          renderHeight = 1080;             // (independent of the window size)
    AntiAliasing aa           = AntiAliasing::MSAA;
    int          msaaSamples  = 2;                // only used when aa == MSAA (2, 4, 8, ...)
    bool         vsync        = false;            // applied by the Window/context, not the renderer
    bool         debugColliders = false;          // draw collider/hurtbox wireframes (initial state; toggled at runtime)
};
