#include "Render/GL/OpenGLRenderer.h"
#include "Render/API/IModel.h"   // complete type needed to call IModel::Draw
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>
#include <iostream>
#include <string>

OpenGLRenderer::OpenGLRenderer(GLADloadproc loader) : m_loader(loader) {}

OpenGLRenderer::~OpenGLRenderer() {
    destroyTargets();
    if (m_quadVbo) glDeleteBuffers(1, &m_quadVbo);
    if (m_quadVao) glDeleteVertexArrays(1, &m_quadVao);
    if (m_wireCubeVbo)   glDeleteBuffers(1, &m_wireCubeVbo);
    if (m_wireCubeVao)   glDeleteVertexArrays(1, &m_wireCubeVao);
    if (m_wireSphereVbo) glDeleteBuffers(1, &m_wireSphereVbo);
    if (m_wireSphereVao) glDeleteVertexArrays(1, &m_wireSphereVao);
    if (m_lineVbo)       glDeleteBuffers(1, &m_lineVbo);
    if (m_lineVao)       glDeleteVertexArrays(1, &m_lineVao);
}

bool OpenGLRenderer::init(int fbW, int fbH) {
    if (!m_glLoaded) {
        if (!gladLoadGLLoader(m_loader)) {
            std::cerr << "OpenGLRenderer: failed to initialize GLAD\n";
            return false;
        }
        m_glLoaded = true;
    }
    m_fbWidth  = fbW;
    m_fbHeight = (fbH == 0) ? 1 : fbH;

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_MULTISAMPLE);   // the multisample FBO drives AA, not global state

    // Built-in shaders (the GL backend owns them; RenderItems pick a material, not a shader).
    m_lit   = std::make_unique<Shader>("assets/shaders/advanced_lighting.vs", "assets/shaders/advanced_lighting.fs");
    m_unlit = std::make_unique<Shader>("assets/shaders/light_s.vs",           "assets/shaders/light_s.fs");
    m_post  = std::make_unique<Shader>("assets/shaders/msaa_post.vs",         "assets/shaders/msaa_post.fs");
    m_post->use();
    m_post->setInt("screenTexture", 0);

    buildQuad();
    buildDebugGeometry();
    buildTargets();
    return true;
}

void OpenGLRenderer::buildQuad() {
    // Fullscreen quad in NDC; the present pass scales it into the letterbox rect.
    const float quad[] = {
        // pos        uv
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    glGenVertexArrays(1, &m_quadVao);
    glGenBuffers(1, &m_quadVbo);
    glBindVertexArray(m_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void OpenGLRenderer::buildDebugGeometry() {
    // Unit wire cube spanning [-0.5, 0.5]^3 as 12 edges (24 verts, GL_LINES). A per-draw
    // model matrix scales it to a collider's world AABB.
    const float h = 0.5f;
    const float cube[] = {
        // bottom square (y = -h)
        -h,-h,-h,  h,-h,-h,   h,-h,-h,  h,-h, h,   h,-h, h, -h,-h, h,  -h,-h, h, -h,-h,-h,
        // top square (y = +h)
        -h, h,-h,  h, h,-h,   h, h,-h,  h, h, h,   h, h, h, -h, h, h,  -h, h, h, -h, h,-h,
        // vertical edges
        -h,-h,-h, -h, h,-h,   h,-h,-h,  h, h,-h,   h,-h, h,  h, h, h,  -h,-h, h, -h, h, h,
    };
    glGenVertexArrays(1, &m_wireCubeVao);
    glGenBuffers(1, &m_wireCubeVbo);
    glBindVertexArray(m_wireCubeVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_wireCubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Unit wire sphere (radius 1): three great-circle rings (XY, XZ, YZ), each N segments
    // emitted as line pairs. Per-draw model scales by the radius.
    const int N = 24;
    std::vector<float> sphere;
    sphere.reserve(3 * N * 2 * 3);
    auto ring = [&](int axis) {
        for (int i = 0; i < N; ++i) {
            const float a0 = (float)i       / N * 6.2831853f;
            const float a1 = (float)(i + 1) / N * 6.2831853f;
            const float c0 = std::cos(a0), s0 = std::sin(a0);
            const float c1 = std::cos(a1), s1 = std::sin(a1);
            glm::vec3 p0, p1;
            if (axis == 0) { p0 = {0, c0, s0}; p1 = {0, c1, s1}; }       // YZ plane
            else if (axis == 1) { p0 = {c0, 0, s0}; p1 = {c1, 0, s1}; }  // XZ plane
            else { p0 = {c0, s0, 0}; p1 = {c1, s1, 0}; }                 // XY plane
            sphere.insert(sphere.end(), {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z});
        }
    };
    ring(0); ring(1); ring(2);
    m_wireSphereVerts = (int)sphere.size() / 3;
    glGenVertexArrays(1, &m_wireSphereVao);
    glGenBuffers(1, &m_wireSphereVbo);
    glBindVertexArray(m_wireSphereVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_wireSphereVbo);
    glBufferData(GL_ARRAY_BUFFER, sphere.size() * sizeof(float), sphere.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    // Dynamic two-vertex buffer; drawLine re-uploads the endpoints each call.
    glGenVertexArrays(1, &m_lineVao);
    glGenBuffers(1, &m_lineVbo);
    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glBindVertexArray(0);
}

void OpenGLRenderer::destroyTargets() {
    if (m_sceneDepth)   { glDeleteRenderbuffers(1, &m_sceneDepth); m_sceneDepth = 0; }
    if (m_sceneColor)   { glDeleteTextures(1, &m_sceneColor);      m_sceneColor = 0; }
    if (m_sceneFbo)     { glDeleteFramebuffers(1, &m_sceneFbo);    m_sceneFbo = 0; }
    if (m_resolveColor) { glDeleteTextures(1, &m_resolveColor);    m_resolveColor = 0; }
    if (m_resolveFbo)   { glDeleteFramebuffers(1, &m_resolveFbo);  m_resolveFbo = 0; }
    m_presentTex = 0;
}

void OpenGLRenderer::buildTargets() {
    destroyTargets();
    const int rw = m_settings.renderWidth;
    const int rh = m_settings.renderHeight;
    m_multisampled = (m_settings.aa == AntiAliasing::MSAA && m_settings.msaaSamples > 1);
    const int samples = m_multisampled ? m_settings.msaaSamples : 1;

    glGenFramebuffers(1, &m_sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);

    if (m_multisampled) {
        // Multisample color texture + matching multisample depth renderbuffer.
        glGenTextures(1, &m_sceneColor);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, m_sceneColor);
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGB, rw, rh, GL_TRUE);
        glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, m_sceneColor, 0);

        glGenRenderbuffers(1, &m_sceneDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, m_sceneDepth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, rw, rh);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_sceneDepth);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "OpenGLRenderer: MSAA framebuffer is incomplete\n";

        // Single-sample resolve target that the present pass samples.
        glGenFramebuffers(1, &m_resolveFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
        glGenTextures(1, &m_resolveColor);
        glBindTexture(GL_TEXTURE_2D, m_resolveColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, rw, rh, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveColor, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "OpenGLRenderer: resolve framebuffer is incomplete\n";

        m_presentTex = m_resolveColor;
    } else {
        // No MSAA: single-sample color texture sampled directly by the present pass.
        glGenTextures(1, &m_sceneColor);
        glBindTexture(GL_TEXTURE_2D, m_sceneColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, rw, rh, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColor, 0);

        glGenRenderbuffers(1, &m_sceneDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, m_sceneDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, rw, rh);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_sceneDepth);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "OpenGLRenderer: scene framebuffer is incomplete\n";

        m_presentTex = m_sceneColor;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLRenderer::applySettings(const RenderSettings& s) {
    const bool targetsChanged =
        s.renderWidth  != m_settings.renderWidth  ||
        s.renderHeight != m_settings.renderHeight ||
        s.aa           != m_settings.aa           ||
        s.msaaSamples  != m_settings.msaaSamples;
    m_settings = s;
    if (m_glLoaded && targetsChanged) buildTargets();
}

void OpenGLRenderer::onResize(int w, int h) {
    m_fbWidth  = w;
    m_fbHeight = (h == 0) ? 1 : h;   // present pass uses this for the letterbox rect
}

void OpenGLRenderer::beginScene(const CameraView& cam) {
    m_camera = cam;

    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glViewport(0, 0, m_settings.renderWidth, m_settings.renderHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Push camera matrices to both built-in shaders up front.
    m_lit->use();
    m_lit->setMat4("view", cam.view);
    m_lit->setMat4("projection", cam.projection);
    m_lit->setVec3("viewPos", cam.position.x, cam.position.y, cam.position.z);
    m_lit->setFloat("shininess", 32.0f);

    m_unlit->use();
    m_unlit->setMat4("view", cam.view);
    m_unlit->setMat4("projection", cam.projection);
}

void OpenGLRenderer::setLights(const LightSet& lights) {
    m_lights = lights;
    m_lit->use();

    // Directional "sun".
    m_lit->setVec3("dirLight.direction", lights.dir.direction.x, lights.dir.direction.y, lights.dir.direction.z);
    m_lit->setVec3("dirLight.ambient",   lights.dir.ambient.x,   lights.dir.ambient.y,   lights.dir.ambient.z);
    m_lit->setVec3("dirLight.diffuse",   lights.dir.diffuse.x,   lights.dir.diffuse.y,   lights.dir.diffuse.z);
    m_lit->setVec3("dirLight.specular",  lights.dir.specular.x,  lights.dir.specular.y,  lights.dir.specular.z);

    // Point lights: compact only the ENABLED ones to the front of the array and tell
    // the shader how many there are (numPointLights), so its loop skips everything
    // else. Capped at the shader's array size.
    const int MAX_POINT_LIGHTS = 4;
    int np = 0;
    for (const PointLight& pl : lights.points) {
        if (!pl.enabled || np >= MAX_POINT_LIGHTS) continue;
        const std::string p = "pointLights[" + std::to_string(np) + "].";
        m_lit->setVec3 (p + "position",  pl.position.x, pl.position.y, pl.position.z);
        m_lit->setFloat(p + "constant",  pl.constant);
        m_lit->setFloat(p + "linear",    pl.linear);
        m_lit->setFloat(p + "quadratic", pl.quadratic);
        m_lit->setFloat(p + "attenuation", pl.attenuation);
        m_lit->setVec3 (p + "ambient",   pl.ambient.x,  pl.ambient.y,  pl.ambient.z);
        m_lit->setVec3 (p + "diffuse",   pl.diffuse.x,  pl.diffuse.y,  pl.diffuse.z);
        m_lit->setVec3 (p + "specular",  pl.specular.x, pl.specular.y, pl.specular.z);
        ++np;
    }
    m_lit->setInt("numPointLights", np);

    // Spot/cone lights (the camera flashlight is just one of them): same compaction.
    // Only enabled lights are uploaded, so there are no zero slots to guard against.
    const int MAX_SPOT_LIGHTS = 4;
    int ns = 0;
    for (const SpotLight& s : lights.spots) {
        if (!s.enabled || ns >= MAX_SPOT_LIGHTS) continue;
        const std::string sp = "spotLights[" + std::to_string(ns) + "].";
        m_lit->setVec3 (sp + "position",    s.position.x,  s.position.y,  s.position.z);
        m_lit->setVec3 (sp + "direction",   s.direction.x, s.direction.y, s.direction.z);
        m_lit->setFloat(sp + "cutOff",      s.cutOff);
        m_lit->setFloat(sp + "outerCutOff", s.outerCutOff);
        m_lit->setFloat(sp + "constant",    s.constant);
        m_lit->setFloat(sp + "linear",      s.linear);
        m_lit->setFloat(sp + "quadratic",   s.quadratic);
        m_lit->setVec3 (sp + "ambient",     0.0f, 0.0f, 0.0f);
        m_lit->setVec3 (sp + "diffuse",     s.diffuse.x,  s.diffuse.y,  s.diffuse.z);
        m_lit->setVec3 (sp + "specular",    s.specular.x, s.specular.y, s.specular.z);
        ++ns;
    }
    m_lit->setInt("numSpotLights", ns);
}

void OpenGLRenderer::submit(const RenderItem& item) {
    if (!item.model) return;

    Shader* sh = (item.material == MaterialKind::Lit) ? m_lit.get() : m_unlit.get();
    sh->use();
    sh->setMat4("model", item.modelMatrix);
    if (item.material == MaterialKind::Unlit)
        sh->setVec3("lightColor", item.unlitColor.x, item.unlitColor.y, item.unlitColor.z);

    if (item.wireframe) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        item.model->Draw(*sh);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else {
        item.model->Draw(*sh);
    }
}

// --- debug primitives ---------------------------------------------------------
// All three draw flat-colored GL_LINES with the unlit shader. view/projection were
// uploaded to m_unlit in beginScene(); we only set model + lightColor here.

void OpenGLRenderer::drawWireBox(const glm::mat4& transform, const glm::vec3& color) {
    m_unlit->use();
    m_unlit->setMat4("model", transform);
    m_unlit->setVec3("lightColor", color.x, color.y, color.z);
    glBindVertexArray(m_wireCubeVao);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
}

void OpenGLRenderer::drawWireSphere(const glm::vec3& center, float radius, const glm::vec3& color) {
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), center)
                          * glm::scale(glm::mat4(1.0f), glm::vec3(radius));
    m_unlit->use();
    m_unlit->setMat4("model", model);
    m_unlit->setVec3("lightColor", color.x, color.y, color.z);
    glBindVertexArray(m_wireSphereVao);
    glDrawArrays(GL_LINES, 0, m_wireSphereVerts);
    glBindVertexArray(0);
}

void OpenGLRenderer::drawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec3& color) {
    const float verts[6] = { a.x, a.y, a.z, b.x, b.y, b.z };
    m_unlit->use();
    m_unlit->setMat4("model", glm::mat4(1.0f));   // endpoints are already world-space
    m_unlit->setVec3("lightColor", color.x, color.y, color.z);
    glBindVertexArray(m_lineVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0);
}

void OpenGLRenderer::endScene() {
    const int rw = m_settings.renderWidth;
    const int rh = m_settings.renderHeight;

    // Resolve MSAA into the single-sample resolve FBO (no-op when AA is off).
    if (m_multisampled) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
        glBlitFramebuffer(0, 0, rw, rh, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // Present: draw the resolved texture into the letterbox/pillarbox region of the
    // window (default framebuffer). The viewport handles the scaling.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, m_fbWidth, m_fbHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const float renderAspect = (float)rw / (float)rh;
    const float windowAspect = (float)m_fbWidth / (float)m_fbHeight;
    int dx, dy, dw, dh;
    if (windowAspect > renderAspect) {
        dh = m_fbHeight;
        dw = (int)(m_fbHeight * renderAspect);
        dx = (m_fbWidth - dw) / 2;
        dy = 0;
    } else {
        dw = m_fbWidth;
        dh = (int)(m_fbWidth / renderAspect);
        dx = 0;
        dy = (m_fbHeight - dh) / 2;
    }
    glViewport(dx, dy, dw, dh);

    m_post->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_presentTex);
    glBindVertexArray(m_quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
