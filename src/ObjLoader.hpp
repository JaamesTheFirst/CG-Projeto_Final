#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct VertexPNT {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 texCoord{};
};

struct MaterialDefinition {
    std::string name;
    glm::vec3 diffuseColor{0.8f};
    float shininess = 32.0f;
    std::filesystem::path diffuseTexture;
};

struct MeshChunk {
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    MaterialDefinition material;
};

struct ObjMesh {
    std::vector<VertexPNT> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshChunk> chunks;
};

bool LoadObjMesh(const std::filesystem::path& objPath,
                 ObjMesh& outMesh,
                 std::string* errorMessage = nullptr);


