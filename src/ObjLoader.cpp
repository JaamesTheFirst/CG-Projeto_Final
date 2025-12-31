#include "ObjLoader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include <glm/geometric.hpp>

namespace {

struct VertexKey {
    int position = 0;
    int texCoord = 0;
    int normal = 0;

    bool operator==(const VertexKey& other) const {
        return position == other.position &&
               texCoord == other.texCoord &&
               normal == other.normal;
    }
};

struct VertexKeyHasher {
    std::size_t operator()(const VertexKey& key) const {
        std::size_t seed = static_cast<std::size_t>(key.position);
        seed ^= static_cast<std::size_t>(key.texCoord) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= static_cast<std::size_t>(key.normal) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

int ResolveIndex(int idx, std::size_t count) {
    if (idx > 0) {
        int resolved = idx - 1;
        return (resolved >= 0 && static_cast<std::size_t>(resolved) < count) ? resolved : -1;
    }
    if (idx < 0) {
        int resolved = static_cast<int>(count) + idx;
        return (resolved >= 0 && static_cast<std::size_t>(resolved) < count) ? resolved : -1;
    }
    return -1;
}

bool ParseFaceToken(const std::string& token, int& v, int& t, int& n) {
    v = t = n = 0;
    std::size_t firstSlash = token.find('/');
    if (firstSlash == std::string::npos) {
        v = std::stoi(token);
        return true;
    }

    std::size_t secondSlash = token.find('/', firstSlash + 1);
    v = std::stoi(token.substr(0, firstSlash));

    if (secondSlash == std::string::npos) {
        if (firstSlash + 1 < token.size()) {
            t = std::stoi(token.substr(firstSlash + 1));
        }
    } else {
        if (secondSlash > firstSlash + 1) {
            t = std::stoi(token.substr(firstSlash + 1, secondSlash - firstSlash - 1));
        }
        if (secondSlash + 1 < token.size()) {
            n = std::stoi(token.substr(secondSlash + 1));
        }
    }
    return true;
}

std::string Trim(const std::string& str) {
    const char* whitespace = " \t\r\n";
    std::size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return {};
    }
    std::size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

void ParseMtlFile(const std::filesystem::path& filePath,
                  std::unordered_map<std::string, MaterialDefinition>& materials) {
    std::ifstream file(filePath);
    if (!file) {
        return;
    }

    MaterialDefinition current;
    std::string currentName;
    std::string line;

    auto commitCurrent = [&]() {
        if (!currentName.empty()) {
            materials[currentName] = current;
        }
    };

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "newmtl") {
            commitCurrent();
            current = MaterialDefinition{};
            current.diffuseColor = glm::vec3(0.8f);
            current.shininess = 32.0f;
            iss >> currentName;
        } else if (token == "Kd") {
            iss >> current.diffuseColor.r >> current.diffuseColor.g >> current.diffuseColor.b;
        } else if (token == "Ns") {
            iss >> current.shininess;
        } else if (token == "map_Kd") {
            std::string texName;
            iss >> texName;
            current.diffuseTexture = (filePath.parent_path() / texName).lexically_normal();
        }
    }

    commitCurrent();
}

MaterialDefinition ResolveMaterial(const std::string& name,
                                   const std::unordered_map<std::string, MaterialDefinition>& library,
                                   const MaterialDefinition& fallback) {
    auto it = library.find(name);
    if (it != library.end()) {
        return it->second;
    }
    return fallback;
}

} // namespace

bool LoadObjMesh(const std::filesystem::path& objPath,
                 ObjMesh& outMesh,
                 std::string* errorMessage) {
    std::ifstream file(objPath);
    if (!file) {
        if (errorMessage) {
            *errorMessage = "Unable to open OBJ file: " + objPath.string();
        }
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> normals;
    std::unordered_map<std::string, MaterialDefinition> materialLibrary;

    ObjMesh mesh;
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.chunks.clear();

    MaterialDefinition defaultMaterial;
    defaultMaterial.name = "default";
    defaultMaterial.diffuseColor = glm::vec3(0.8f);
    defaultMaterial.shininess = 32.0f;

    MeshChunk currentChunk;
    currentChunk.material = defaultMaterial;
    currentChunk.startIndex = 0;
    currentChunk.indexCount = 0;
    std::string currentMaterialName;

    std::unordered_map<VertexKey, uint32_t, VertexKeyHasher> vertexCache;

    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "v") {
            glm::vec3 pos{};
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        } else if (token == "vt") {
            glm::vec2 uv{};
            iss >> uv.x >> uv.y;
            texcoords.push_back(uv);
        } else if (token == "vn") {
            glm::vec3 normal{};
            iss >> normal.x >> normal.y >> normal.z;
            normals.push_back(normal);
        } else if (token == "mtllib") {
            std::string mtlFile;
            while (iss >> mtlFile) {
                ParseMtlFile((objPath.parent_path() / mtlFile).lexically_normal(), materialLibrary);
            }
        } else if (token == "usemtl") {
            std::string materialName;
            iss >> materialName;
            if (materialName != currentMaterialName) {
                if (currentChunk.indexCount > 0) {
                    mesh.chunks.push_back(currentChunk);
                    currentChunk.startIndex = static_cast<uint32_t>(mesh.indices.size());
                    currentChunk.indexCount = 0;
                } else {
                    currentChunk.startIndex = static_cast<uint32_t>(mesh.indices.size());
                }
                currentMaterialName = materialName;
                currentChunk.material = ResolveMaterial(materialName, materialLibrary, defaultMaterial);
            }
        } else if (token == "f") {
            std::vector<std::string> faceTokens;
            std::string part;
            while (iss >> part) {
                faceTokens.push_back(part);
            }
            if (faceTokens.size() < 3) {
                continue;
            }

            auto emitVertex = [&](const std::string& partToken) -> int {
                int vi = 0, ti = 0, ni = 0;
                if (!ParseFaceToken(partToken, vi, ti, ni)) {
                    return -1;
                }
                int posIndex = ResolveIndex(vi, positions.size());
                if (posIndex < 0) {
                    return -1;
                }
                int texIndex = ResolveIndex(ti, texcoords.size());
                int normIndex = ResolveIndex(ni, normals.size());

                VertexKey key{posIndex, texIndex, normIndex};
                auto it = vertexCache.find(key);
                if (it != vertexCache.end()) {
                    return static_cast<int>(it->second);
                }

                VertexPNT vertex{};
                vertex.position = positions[posIndex];
                if (texIndex >= 0) {
                    vertex.texCoord = texcoords[texIndex];
                }
                if (normIndex >= 0) {
                    vertex.normal = normals[normIndex];
                }

                uint32_t newIndex = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
                vertexCache.emplace(key, newIndex);
                return static_cast<int>(newIndex);
            };

            // Triangulate polygon via fan method
            int first = emitVertex(faceTokens[0]);
            int prev = emitVertex(faceTokens[1]);
            for (std::size_t i = 2; i < faceTokens.size(); ++i) {
                int current = emitVertex(faceTokens[i]);
                if (first < 0 || prev < 0 || current < 0) {
                    continue;
                }
                mesh.indices.push_back(static_cast<uint32_t>(first));
                mesh.indices.push_back(static_cast<uint32_t>(prev));
                mesh.indices.push_back(static_cast<uint32_t>(current));
                currentChunk.indexCount += 3;
                prev = current;
            }
        }
    }

    if (currentChunk.indexCount > 0) {
        mesh.chunks.push_back(currentChunk);
    }

    // Compute normals if none were provided
    bool hasNormals = false;
    for (const auto& v : mesh.vertices) {
        if (glm::dot(v.normal, v.normal) > 0.0f) {
            hasNormals = true;
            break;
        }
    }

    if (!hasNormals) {
        for (auto& v : mesh.vertices) {
            v.normal = glm::vec3(0.0f);
        }
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            VertexPNT& a = mesh.vertices[mesh.indices[i]];
            VertexPNT& b = mesh.vertices[mesh.indices[i + 1]];
            VertexPNT& c = mesh.vertices[mesh.indices[i + 2]];
            glm::vec3 ab = b.position - a.position;
            glm::vec3 ac = c.position - a.position;
            glm::vec3 normal = glm::normalize(glm::cross(ab, ac));
            if (glm::any(glm::isnan(normal))) {
                continue;
            }
            a.normal += normal;
            b.normal += normal;
            c.normal += normal;
        }
        for (auto& v : mesh.vertices) {
            if (glm::dot(v.normal, v.normal) > 0.0f) {
                v.normal = glm::normalize(v.normal);
            } else {
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
    } else {
        for (auto& v : mesh.vertices) {
            if (glm::dot(v.normal, v.normal) > 0.0f) {
                v.normal = glm::normalize(v.normal);
            }
        }
    }

    outMesh = std::move(mesh);
    return true;
}


