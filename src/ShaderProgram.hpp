#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <string>

#include <GL/glew.h>

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    bool LoadFromFiles(const std::filesystem::path& vertexPath,
                       const std::filesystem::path& fragmentPath,
                       std::string* error = nullptr);

    void Use() const;
    GLuint GetHandle() const { return program_; }

    void SetMat4(const std::string& name, const glm::mat4& value) const;
    void SetMat3(const std::string& name, const glm::mat3& value) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;
    void SetVec4(const std::string& name, const glm::vec4& value) const;
    void SetFloat(const std::string& name, float value) const;
    void SetInt(const std::string& name, int value) const;

private:
    GLuint program_ = 0;

    GLint GetUniformLocation(const std::string& name) const;
    GLuint CompileShader(GLenum type, const std::string& source, std::string& error);
    static bool ReadFile(const std::filesystem::path& path, std::string& out, std::string* error);
    void Destroy();
};


