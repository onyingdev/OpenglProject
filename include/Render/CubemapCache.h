#pragma once
#include <glad/glad.h>
#include "Core/Engine/Helpers/stb_image.h"
#include <array>
#include <string>
#include <unordered_map>
#include <iostream>

// Loads 6 face images into an OpenGL cube map and (optionally) caches the result by the joined face
// paths, so multiple reflective objects sharing one environment share a single GPU upload. Faces are
// color data, so an sRGB internal format is used — samples come back LINEAR, matching the PBR pipeline.
//
// Two entry points with different ownership:
//   - loadCubemapGL(): pure loader, caller owns/deletes the texture (the renderer's skybox uses this).
//   - get():           cached; the cache owns the textures and frees them in clear() (scene unload).
namespace CubemapCache {

    // Pure loader: 6 faces -> GL cube texture id. Returns 0 (freeing any partial state) if a face is
    // missing, so callers can disable reflection / skybox gracefully. faces are in GL order:
    //   +X, -X, +Y, -Y, +Z, -Z  (right, left, top, bottom, front, back).
    inline unsigned int loadCubemapGL(const std::array<std::string, 6>& faces) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);

        // Cube-map faces are authored top-down and, unlike 2D model textures, must NOT be flipped.
        // Restore the engine-wide default (true; set in Engine bootstrap) when done.
        stbi_set_flip_vertically_on_load(false);

        int w, h, n;
        for (unsigned i = 0; i < 6; ++i) {
            unsigned char* data = stbi_load(faces[i].c_str(), &w, &h, &n, 0);
            if (!data) {
                std::cerr << "CubemapCache: face failed to load: " << faces[i] << "\n";
                stbi_image_free(data);
                glDeleteTextures(1, &tex);
                stbi_set_flip_vertically_on_load(true);
                return 0;
            }
            const GLenum src  = (n == 4) ? GL_RGBA          : (n == 1 ? GL_RED : GL_RGB);
            const GLint  ifmt = (n == 4) ? GL_SRGB8_ALPHA8  : (n == 1 ? GL_RED : GL_SRGB8);
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, ifmt, w, h, 0,
                         src, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        stbi_set_flip_vertically_on_load(true);

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return tex;
    }

    namespace detail {
        inline std::unordered_map<std::string, unsigned int>& table() {
            static std::unordered_map<std::string, unsigned int> t;
            return t;
        }
    }

    // Cached load: shares one GL cube map per identical face set. Returns 0 on failure (not cached).
    inline unsigned int get(const std::array<std::string, 6>& faces) {
        std::string key;
        for (const auto& f : faces) { key += f; key += '|'; }
        auto& t = detail::table();
        if (auto it = t.find(key); it != t.end()) return it->second;
        unsigned int tex = loadCubemapGL(faces);
        if (tex) t[key] = tex;
        return tex;
    }

    // Drop every cached cube map (call on scene unload / shutdown while the GL context is alive).
    // Mirrors ModelCache::clear; objects must re-fetch after a reload.
    inline void clear() {
        auto& t = detail::table();
        for (auto& [k, tex] : t) { GLuint g = (GLuint)tex; glDeleteTextures(1, &g); }
        t.clear();
    }
}
