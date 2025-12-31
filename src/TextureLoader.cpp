#include "TextureLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace gfx {
namespace {

struct FileCloser {
    void operator()(FILE* file) const {
        if (file) {
            std::fclose(file);
        }
    }
};

} // namespace

bool LoadTexture2D(const std::filesystem::path& path,
                   GLuint& outTexture,
                   std::string* error) {
    stbi_set_flip_vertically_on_load(0);
    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
    if (!data) {
        if (error) {
            *error = "Failed to load image: " + std::string(stbi_failure_reason());
        }
        return false;
    }

    GLenum format = GL_RGB;
    GLenum internalFormat = GL_SRGB8;
    if (channels == 4) {
        format = GL_RGBA;
        internalFormat = GL_SRGB8_ALPHA8;
    } else if (channels == 3) {
        format = GL_RGB;
        internalFormat = GL_SRGB8;
    } else if (channels == 1) {
        format = GL_RED;
        internalFormat = GL_R8;
    }

    glGenTextures(1, &outTexture);
    glBindTexture(GL_TEXTURE_2D, outTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return true;
}

bool LoadTexture2DFromMemory(const unsigned char* data,
                             std::size_t size,
                             GLuint& outTexture,
                             std::string* error) {
    if (!data || size == 0) {
        if (error) {
            *error = "Image memory buffer is empty.";
        }
        return false;
    }

    stbi_set_flip_vertically_on_load(0);
    int width = 0, height = 0, channels = 0;
    unsigned char* imgData = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 0);
    if (!imgData) {
        if (error) {
            *error = "Failed to load image from memory: " + std::string(stbi_failure_reason());
        }
        return false;
    }

    GLenum format = GL_RGB;
    GLenum internalFormat = GL_SRGB8;
    if (channels == 4) {
        format = GL_RGBA;
        internalFormat = GL_SRGB8_ALPHA8;
    } else if (channels == 3) {
        format = GL_RGB;
        internalFormat = GL_SRGB8;
    } else if (channels == 1) {
        format = GL_RED;
        internalFormat = GL_R8;
    }

    glGenTextures(1, &outTexture);
    glBindTexture(GL_TEXTURE_2D, outTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, imgData);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(imgData);
    return true;
}

} // namespace gfx


