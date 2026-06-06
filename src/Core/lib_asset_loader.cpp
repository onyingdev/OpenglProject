#include <glad/glad.h>
#include "Core/Engine/lib_asset_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "Core/Engine/stb_image.h"

#include <iostream>
#include <string>
using namespace std;

namespace AssetLoaderLib {
    unsigned int load_texture(const char* texturePath, bool vertical_flip) {
        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        //define texture wrapping, filtering and mipmaps
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_set_flip_vertically_on_load(vertical_flip);

        int width, height, nrChannels;
        unsigned char *data = stbi_load(texturePath, &width, &height, &nrChannels, 0);

        if (data) {
           GLenum format = GL_RGB;
           if (nrChannels == 4) {
               format = GL_RGBA; // PNG with transparency
           } else if (nrChannels == 1) {
               format = GL_RED;  // Grayscale/Heightmaps
           }
           glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
           glGenerateMipmap(GL_TEXTURE_2D);
       }
       else {
           std::cout << "Failed to load texture at path: " << texturePath << "\n";
       }

        //free image memory IMPORTANT!
        stbi_image_free(data);
        return texture;
    }
}

