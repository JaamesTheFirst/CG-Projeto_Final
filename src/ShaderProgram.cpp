#include "ShaderProgram.hpp"

#include <fstream>
#include <sstream>
#include <vector>

ShaderProgram::~ShaderProgram() {
    Destroy();
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept {
    program_ = other.program_;
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        Destroy();
        program_ = other.program_;
        other.program_ = 0;
    }
    return *this;
}

bool ShaderProgram::LoadFromFiles(const std::filesystem::path& vertexPath,
                                  const std::filesystem::path& fragmentPath,
                                  std::string* error) {
    std::string vertexSource;
    std::string fragmentSource;
    if (!ReadFile(vertexPath, vertexSource, error) || !ReadFile(fragmentPath, fragmentSource, error)) {
        return false;
    }

    std::string compileError;
    GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource, compileError);
    if (!vertexShader) {
        if (error) {
            *error = "Vertex shader error: " + compileError;
        }
        return false;
    }

    GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource, compileError);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        if (error) {
            *error = "Fragment shader error: " + compileError;
        }
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint linkStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<GLchar> log(logLength + 1);
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        if (error) {
            *error = "Program link error: " + std::string(log.data());
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(program);
        return false;
    }

    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    Destroy();
    program_ = program;
    return true;
}

void ShaderProgram::Use() const {
    glUseProgram(program_);
}

void ShaderProgram::SetMat4(const std::string& name, const glm::mat4& value) const {
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void ShaderProgram::SetMat3(const std::string& name, const glm::mat3& value) const {
    glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, &value[0][0]);
}

void ShaderProgram::SetVec3(const std::string& name, const glm::vec3& value) const {
    glUniform3fv(GetUniformLocation(name), 1, &value[0]);
}

void ShaderProgram::SetVec4(const std::string& name, const glm::vec4& value) const {
    glUniform4fv(GetUniformLocation(name), 1, &value[0]);
}

void ShaderProgram::SetFloat(const std::string& name, float value) const {
    glUniform1f(GetUniformLocation(name), value);
}

void ShaderProgram::SetInt(const std::string& name, int value) const {
    glUniform1i(GetUniformLocation(name), value);
}

GLint ShaderProgram::GetUniformLocation(const std::string& name) const {
    return glGetUniformLocation(program_, name.c_str());
}

GLuint ShaderProgram::CompileShader(GLenum type, const std::string& source, std::string& error) {
    GLuint shader = glCreateShader(type);
    const GLchar* data = source.c_str();
    glShaderSource(shader, 1, &data, nullptr);
    glCompileShader(shader);

    GLint compileStatus = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<GLchar> log(logLength + 1);
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        error = log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ShaderProgram::ReadFile(const std::filesystem::path& path, std::string& out, std::string* error) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        if (error) {
            *error = "Failed to open shader file: " + path.string();
        }
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

void ShaderProgram::Destroy() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}


