#pragma once

#include <GL/glew.h>
#include <filesystem>
#include <string>

namespace gfx {

// Loads a PNG/JPEG texture into GPU memory (supports PNG via libpng, JPEG via stb_image).
bool LoadTexture2D(const std::filesystem::path& path,
                   GLuint& outTexture,
                   std::string* error = nullptr);

// Loads a PNG texture from a memory buffer.
bool LoadTexture2DFromMemory(const unsigned char* data,
                             std::size_t size,
                             GLuint& outTexture,
                             std::string* error = nullptr);

} // namespace gfx


