#pragma once

#include "ObjLoader.hpp"
#include "ShaderProgram.hpp"

#include <vector>

struct MeshDrawCall {
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    glm::vec4 baseColorFactor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic = 0.0f;
    float roughness = 1.0f;
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
    GLuint baseColorTexture = 0;
    GLuint metallicRoughnessTexture = 0;
    bool hasBaseColorTexture = false;
    bool hasMetallicRoughnessTexture = false;
};

class Model {
public:
    Model() = default;
    ~Model();

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    Model(Model&&) noexcept;
    Model& operator=(Model&&) noexcept;

    bool LoadFromObj(const std::filesystem::path& objPath, std::string* errorMessage = nullptr);
    bool LoadFromGlb(const std::filesystem::path& glbPath, std::string* errorMessage = nullptr);
    bool LoadFromFbx(const std::filesystem::path& fbxPath, std::string* errorMessage = nullptr);
    void Draw(const ShaderProgram& shader) const;
    void Destroy();

    glm::vec3 GetBoundsMin() const { return boundsMin_; }
    glm::vec3 GetBoundsMax() const { return boundsMax_; }
    const std::vector<glm::vec3>& GetColliderMins() const { return colliderMins_; }
    const std::vector<glm::vec3>& GetColliderMaxs() const { return colliderMaxs_; }
    const std::vector<MeshDrawCall>& GetDraws() const { return draws_; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    std::vector<MeshDrawCall> draws_;
    std::vector<GLuint> textures_;
    std::size_t indexCount_ = 0;
    glm::vec3 boundsMin_{0.0f};
    glm::vec3 boundsMax_{0.0f};
    std::vector<glm::vec3> colliderMins_;
    std::vector<glm::vec3> colliderMaxs_;
};


