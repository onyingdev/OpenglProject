#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Render/shader_s.h"
#include <unordered_map>
#include <memory>
#include <cmath>
#include <iostream>

// Bakes + caches the Karis split-sum IBL set for any environment cube map, so per-object reflection
// probes share one bake. Keyed by the source env cube's GL id:
//   Probe = { irradiance (diffuse), prefilter (roughness-mipped specular) }.
// The BRDF LUT is environment-independent, so it is baked ONCE for the program's life (not per probe,
// not cleared on unload). Probes are cleared wholesale at scene reset/unload (alongside CubemapCache).
//
// All GL work (capture shaders, VAOs, FBO, the bakes) happens lazily on the GL thread on first get().
// get()/brdfLUT() leave the default framebuffer bound (0); the caller rebinds its own target after.
class IblProbeCache {
public:
    struct Probe { unsigned int irradiance = 0, prefilter = 0; };

    static IblProbeCache& instance() { static IblProbeCache c; return c; }

    // Probe for an environment cube map, baking on first request. Returns a stable reference (the map
    // isn't reorganized destructively between frames). envCube must be a valid GL cube texture.
    const Probe& get(unsigned int envCube) {
        auto it = m_probes.find(envCube);
        if (it != m_probes.end()) return it->second;

        ensureResources();

        // The prefilter mip-sampling firefly fix reads coarse mips of the environment, so give the input
        // cube a mip chain (harmless for the skybox background it may also serve).
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        int srcRes = 512;
        glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_TEXTURE_WIDTH, &srcRes);

        Probe p;
        p.irradiance = bakeIrradiance(envCube);
        p.prefilter  = bakePrefilter(envCube, (float)srcRes);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        std::cout << "IblProbeCache: baked probe for env cube " << envCube
                  << " (src " << srcRes << ")\n";
        return m_probes.emplace(envCube, p).first->second;
    }

    // Environment BRDF LUT (512^2 RG16F). Baked once; persists for the program's life.
    unsigned int brdfLUT() {
        if (m_brdfLUT) return m_brdfLUT;
        ensureResources();
        m_brdfLUT = bakeBrdfLUT();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return m_brdfLUT;
    }

    // Free every baked probe (call at scene reset/unload, alongside CubemapCache::clear, while the GL
    // context is alive). Keeps the BRDF LUT and the lazily-created capture resources.
    void clear() {
        for (auto& [env, p] : m_probes) {
            if (p.irradiance) { GLuint t = p.irradiance; glDeleteTextures(1, &t); }
            if (p.prefilter)  { GLuint t = p.prefilter;  glDeleteTextures(1, &t); }
        }
        m_probes.clear();
    }

private:
    IblProbeCache() = default;

    std::unordered_map<unsigned int, Probe> m_probes;   // keyed by source env cube GL id
    unsigned int m_brdfLUT = 0;

    // Lazily-created capture resources (GL thread).
    std::unique_ptr<Shader> m_irradianceSh, m_prefilterSh, m_brdfSh;
    GLuint m_cubeVao = 0, m_cubeVbo = 0;   // unit cube, position only (render direction = position)
    GLuint m_quadVao = 0, m_quadVbo = 0;   // fullscreen quad (vec2 pos + vec2 uv) for the BRDF pass
    GLuint m_fbo = 0, m_rbo = 0;
    bool   m_ready = false;

    void ensureResources() {
        if (m_ready) return;
        m_ready = true;

        m_irradianceSh = std::make_unique<Shader>("assets/shaders/cubemap_capture.vs", "assets/shaders/irradiance_convolution.fs");
        m_prefilterSh  = std::make_unique<Shader>("assets/shaders/cubemap_capture.vs", "assets/shaders/prefilter.fs");
        m_brdfSh       = std::make_unique<Shader>("assets/shaders/brdf.vs",            "assets/shaders/brdf.fs");

        static const float cube[] = {
            -1,  1, -1,  -1, -1, -1,   1, -1, -1,   1, -1, -1,   1,  1, -1,  -1,  1, -1,
            -1, -1,  1,  -1, -1, -1,  -1,  1, -1,  -1,  1, -1,  -1,  1,  1,  -1, -1,  1,
             1, -1, -1,   1, -1,  1,   1,  1,  1,   1,  1,  1,   1,  1, -1,   1, -1, -1,
            -1, -1,  1,  -1,  1,  1,   1,  1,  1,   1,  1,  1,   1, -1,  1,  -1, -1,  1,
            -1,  1, -1,   1,  1, -1,   1,  1,  1,   1,  1,  1,  -1,  1,  1,  -1,  1, -1,
            -1, -1, -1,  -1, -1,  1,   1, -1, -1,   1, -1, -1,  -1, -1,  1,   1, -1,  1
        };
        glGenVertexArrays(1, &m_cubeVao);
        glGenBuffers(1, &m_cubeVbo);
        glBindVertexArray(m_cubeVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        static const float quad[] = {
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

        glGenFramebuffers(1, &m_fbo);
        glGenRenderbuffers(1, &m_rbo);
    }

    // The 6 cube-face capture views + a 90° projection (shared by both cube bakes).
    static const glm::mat4* captureViews() {
        static const glm::mat4 v[6] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
        };
        return v;
    }

    void drawCube() {
        glBindVertexArray(m_cubeVao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    unsigned int bakeIrradiance(unsigned int envCube) {
        const int RES = 32;
        const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
        for (int i = 0; i < 6; ++i)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, RES, RES, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, RES, RES);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rbo);

        m_irradianceSh->use();
        m_irradianceSh->setInt("environmentMap", 0);
        m_irradianceSh->setMat4("projection", proj);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
        glViewport(0, 0, RES, RES);
        for (int i = 0; i < 6; ++i) {
            m_irradianceSh->setMat4("view", captureViews()[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, tex, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawCube();
        }
        return tex;
    }

    unsigned int bakePrefilter(unsigned int envCube, float srcRes) {
        const int RES = 128;
        const int MIPS = 5;   // matches MAX_REFLECTION_LOD (4) in pbr.fs
        const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
        for (int i = 0; i < 6; ++i)
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, RES, RES, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);   // allocate the mip chain we render into

        m_prefilterSh->use();
        m_prefilterSh->setInt("environmentMap", 0);
        m_prefilterSh->setMat4("projection", proj);
        m_prefilterSh->setFloat("resolution", srcRes);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        for (int mip = 0; mip < MIPS; ++mip) {
            const int mipRes = (int)(RES * std::pow(0.5, mip));
            glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipRes, mipRes);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rbo);
            glViewport(0, 0, mipRes, mipRes);
            const float roughness = (float)mip / (float)(MIPS - 1);
            m_prefilterSh->setFloat("roughness", roughness);
            for (int i = 0; i < 6; ++i) {
                m_prefilterSh->setMat4("view", captureViews()[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, tex, mip);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                drawCube();
            }
        }
        return tex;
    }

    unsigned int bakeBrdfLUT() {
        const int RES = 512;
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, RES, RES, 0, GL_RG, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, RES, RES);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glViewport(0, 0, RES, RES);
        m_brdfSh->use();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(m_quadVao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        return tex;
    }
};
