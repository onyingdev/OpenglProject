#pragma once
#include <glad/glad.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include "Render/API/IRenderer.h"
#include "Render/shader_s.h"

// OpenGL 3.3 implementation of IRenderer. Owns the offscreen scene framebuffer(s),
// the built-in shaders, and the fullscreen present quad. Rebuilds its render targets
// when the resolution or anti-aliasing mode changes at runtime (applySettings).
class OpenGLRenderer : public IRenderer {
public:
    // loader is GLFW's glfwGetProcAddress, cast to GLADloadproc. It lives in the GL
    // backend so the generic IRenderer never sees a platform/GL loader type.
    explicit OpenGLRenderer(GLADloadproc loader);
    ~OpenGLRenderer() override;

    bool init(int framebufferWidth, int framebufferHeight) override;
    void applySettings(const RenderSettings& settings) override;
    const RenderSettings& settings() const override { return m_settings; }
    void onResize(int framebufferWidth, int framebufferHeight) override;

    void beginScene(const CameraView& camera) override;
    void setLights(const LightSet& lights) override;
    void submit(const RenderItem& item) override;
    void endScene() override;

    void drawWireBox(const glm::mat4& transform, const glm::vec3& color) override;
    void drawWireSphere(const glm::vec3& center, float radius, const glm::vec3& color) override;
    void drawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) override;

private:
    void buildTargets();    // (re)create scene/resolve FBOs for the current settings
    void destroyTargets();
    void buildQuad();       // fullscreen present quad (created once)
    void buildDebugGeometry();  // wire-cube + wire-sphere + dynamic line buffers (created once)

    void uploadCamera(Shader& sh);   // push view/projection/viewPos to a program
    void uploadLights(Shader& sh);   // push the frame's light set to a program
    void flushOpaqueBatches();       // draw all deferred lit meshes instanced (idempotent per frame)

    GLADloadproc   m_loader   = nullptr;
    bool           m_glLoaded = false;
    RenderSettings m_settings;

    int m_fbWidth = 0, m_fbHeight = 0;   // default framebuffer (window) size

    // Offscreen targets. When MSAA is on, the scene renders into the multisample
    // FBO and is resolved into the single-sample resolve FBO; the present pass
    // samples m_presentTex (resolve color for MSAA, scene color otherwise).
    GLuint m_sceneFbo = 0, m_sceneColor = 0, m_sceneDepth = 0;
    GLuint m_resolveFbo = 0, m_resolveColor = 0;
    GLuint m_presentTex = 0;
    bool   m_multisampled = false;

    // Fullscreen present quad.
    GLuint m_quadVao = 0, m_quadVbo = 0;

    // Debug gizmo geometry (drawn with m_unlit as GL_LINES). Unit shapes scaled per call.
    GLuint m_wireCubeVao = 0,   m_wireCubeVbo = 0;
    GLuint m_wireSphereVao = 0, m_wireSphereVbo = 0;
    int    m_wireSphereVerts = 0;
    GLuint m_lineVao = 0,       m_lineVbo = 0;   // dynamic: two verts re-uploaded per drawLine

    // Built-in shaders (owned by the GL backend).
    std::unique_ptr<Shader> m_lit;          // advanced_lighting (single draw; model via uniform)
    std::unique_ptr<Shader> m_litInstanced; // advanced_lighting_instanced (model via instance attribute)
    std::unique_ptr<Shader> m_unlit;        // light_s
    std::unique_ptr<Shader> m_post;         // msaa_post (fullscreen present, no post-AA)
    std::unique_ptr<Shader> m_fxaa;         // fxaa_s (present pass with FXAA edge filter)

    CameraView m_camera;
    LightSet   m_lights;

    // Deferred opaque-lit submissions, grouped by model so each group draws in ONE instanced call.
    // Filled by submit(), flushed (once) by flushOpaqueBatches() before debug draw / present.
    std::unordered_map<IModel*, std::vector<glm::mat4>> m_batches;
    bool m_batchesFlushed = false;
};
