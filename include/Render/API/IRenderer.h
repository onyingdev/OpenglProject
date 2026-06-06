#pragma once
#include "Render/API/RenderSettings.h"
#include "Render/API/RenderTypes.h"

// Backend-agnostic renderer. Concrete backends (OpenGL today, Vulkan/other later)
// implement this; the engine submits a camera, lights, and render items per frame
// and never touches a graphics API directly.
//
// Per-frame protocol:
//     beginScene(cam) -> setLights(set) -> submit(item) * N -> endScene()
// endScene() leaves the final, AA-resolved image in the default framebuffer;
// presenting it (the buffer swap) is the Window's job, not the renderer's.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // One-time setup. framebuffer* is the current window/default-framebuffer size,
    // used for the final letterboxed present pass.
    virtual bool init(int framebufferWidth, int framebufferHeight) = 0;

    // Runtime configuration. Changing render resolution or anti-aliasing rebuilds
    // the offscreen targets; everything else stays put.
    virtual void applySettings(const RenderSettings& settings) = 0;
    virtual const RenderSettings& settings() const = 0;

    // The window / default framebuffer was resized (affects only the present pass).
    virtual void onResize(int framebufferWidth, int framebufferHeight) = 0;

    virtual void beginScene(const CameraView& camera) = 0;
    virtual void setLights(const LightSet& lights) = 0;
    virtual void submit(const RenderItem& item) = 0;
    virtual void endScene() = 0;
};
