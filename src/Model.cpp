#include "Model.hpp"

#include "TextureLoader.hpp"
#include "cgltf.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cstddef>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>
#include <set>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

Model::~Model() {
    Destroy();
}

Model::Model(Model&& other) noexcept {
    *this = std::move(other);
}

Model& Model::operator=(Model&& other) noexcept {
    if (this != &other) {
        Destroy();
        vao_ = other.vao_;
        vbo_ = other.vbo_;
        ebo_ = other.ebo_;
        draws_ = std::move(other.draws_);
        textures_ = std::move(other.textures_);
        indexCount_ = other.indexCount_;
        boundsMin_ = other.boundsMin_;
        boundsMax_ = other.boundsMax_;
        colliderMins_ = std::move(other.colliderMins_);
        colliderMaxs_ = std::move(other.colliderMaxs_);

        other.vao_ = 0;
        other.vbo_ = 0;
        other.ebo_ = 0;
        other.indexCount_ = 0;
        other.boundsMin_ = glm::vec3(0.0f);
        other.boundsMax_ = glm::vec3(0.0f);
        other.colliderMins_.clear();
        other.colliderMaxs_.clear();
        other.draws_.clear();
        other.textures_.clear();
    }
    return *this;
}

bool Model::LoadFromObj(const std::filesystem::path& objPath, std::string* errorMessage) {
    ObjMesh mesh;
    if (!LoadObjMesh(objPath, mesh, errorMessage)) {
        return false;
    }

    if (mesh.vertices.empty() || mesh.indices.empty()) {
        if (errorMessage) {
            *errorMessage = "OBJ file does not contain any drawable geometry.";
        }
        return false;
    }

    Destroy();

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 mesh.vertices.size() * sizeof(VertexPNT),
                 mesh.vertices.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 mesh.indices.size() * sizeof(uint32_t),
                 mesh.indices.data(),
                 GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(VertexPNT);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, texCoord)));

    glBindVertexArray(0);

    if (!mesh.vertices.empty()) {
        boundsMin_ = mesh.vertices.front().position;
        boundsMax_ = mesh.vertices.front().position;
        for (const auto& v : mesh.vertices) {
            boundsMin_ = glm::min(boundsMin_, v.position);
            boundsMax_ = glm::max(boundsMax_, v.position);
        }
    } else {
        boundsMin_ = glm::vec3(0.0f);
        boundsMax_ = glm::vec3(0.0f);
    }

    draws_.clear();
    textures_.clear();
    colliderMins_.clear();
    colliderMaxs_.clear();
    std::unordered_map<std::string, GLuint> textureCache;

    auto acquireTexture = [&](const std::filesystem::path& texPath, std::string& lastError) -> GLuint {
        auto canonical = texPath.lexically_normal();
        std::string key = canonical.string();
        auto cacheIt = textureCache.find(key);
        if (cacheIt != textureCache.end()) {
            return cacheIt->second;
        }

        GLuint texture = 0;
        if (!gfx::LoadTexture2D(canonical, texture, &lastError)) {
            return 0;
        }

        textureCache.emplace(key, texture);
        textures_.push_back(texture);
        return texture;
    };

    std::string textureError;
    for (const auto& chunk : mesh.chunks) {
        if (chunk.indexCount == 0) {
            continue;
        }
        MeshDrawCall draw;
        draw.startIndex = chunk.startIndex;
        draw.indexCount = chunk.indexCount;
        draw.baseColorFactor = glm::vec4(chunk.material.diffuseColor, 1.0f);
        draw.metallic = 0.0f;
        draw.roughness = 1.0f;

        if (!chunk.material.diffuseTexture.empty()) {
            GLuint tex = acquireTexture(chunk.material.diffuseTexture, textureError);
            if (tex != 0) {
                draw.baseColorTexture = tex;
                draw.hasBaseColorTexture = true;
            } else if (errorMessage && !textureError.empty()) {
                *errorMessage = "Failed to load texture " + chunk.material.diffuseTexture.string() + ": " + textureError;
            }
        }

        draws_.push_back(draw);
    }

    if (draws_.empty()) {
        MeshDrawCall fallback;
        fallback.startIndex = 0;
        fallback.indexCount = static_cast<uint32_t>(mesh.indices.size());
        draws_.push_back(fallback);
    }

    // Build per-triangle colliders for tighter collision
    colliderMins_.clear();
    colliderMaxs_.clear();
    colliderMins_.reserve(mesh.indices.size() / 3);
    colliderMaxs_.reserve(mesh.indices.size() / 3);
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3& p0 = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3& p1 = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3& p2 = mesh.vertices[mesh.indices[i + 2]].position;
        glm::vec3 mn = glm::min(p0, glm::min(p1, p2));
        glm::vec3 mx = glm::max(p0, glm::max(p1, p2));
        colliderMins_.push_back(mn);
        colliderMaxs_.push_back(mx);
    }

    indexCount_ = mesh.indices.size();
    return true;
}

namespace {

bool LoadCgltfFile(const std::filesystem::path& path, cgltf_data*& outData, std::string* error) {
    cgltf_options options{};
    cgltf_result result = cgltf_parse_file(&options, path.string().c_str(), &outData);
    if (result != cgltf_result_success) {
        if (error) {
            *error = "Failed to parse glTF/GLB: " + std::to_string(result);
        }
        return false;
    }

    result = cgltf_load_buffers(&options, outData, path.string().c_str());
    if (result != cgltf_result_success) {
        if (error) {
            *error = "Failed to load buffers: " + std::to_string(result);
        }
        cgltf_free(outData);
        outData = nullptr;
        return false;
    }
    
    return true;
}

GLuint LoadTextureFromGltfImage(const cgltf_image* image,
                                const std::filesystem::path& basePath,
                                std::string& lastError) {
    GLuint tex = 0;

    if (image->buffer_view && image->buffer_view->buffer &&
        image->buffer_view->buffer->data && image->buffer_view->size > 0) {
        const unsigned char* ptr = static_cast<const unsigned char*>(
            static_cast<const void*>(static_cast<const unsigned char*>(image->buffer_view->buffer->data) +
                                     image->buffer_view->offset));
        std::size_t sz = image->buffer_view->size;
        if (!gfx::LoadTexture2DFromMemory(ptr, sz, tex, &lastError)) {
            return 0;
        }
        return tex;
    }

    if (image->uri && *image->uri) {
        std::filesystem::path texPath = basePath / image->uri;
        if (!gfx::LoadTexture2D(texPath, tex, &lastError)) {
            return 0;
        }
        return tex;
    }

    lastError = "Image has no data or uri.";
    return 0;
}

const cgltf_accessor* FindAttribute(const cgltf_primitive& prim, cgltf_attribute_type type) {
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].type == type) {
            return prim.attributes[i].data;
        }
    }
    return nullptr;
}

} // namespace

bool Model::LoadFromGlb(const std::filesystem::path& glbPath, std::string* errorMessage) {
    cgltf_data* data = nullptr;
    if (!LoadCgltfFile(glbPath, data, errorMessage)) {
        return false;
    }

    if (!data || data->meshes_count == 0) {
        if (errorMessage) {
            *errorMessage = "GLB contains no meshes.";
        }
        if (data) {
            cgltf_free(data);
        }
        return false;
    }

    Destroy();

    std::vector<VertexPNT> vertices;
    std::vector<uint32_t> indices;
    draws_.clear();
    textures_.clear();

    std::unordered_map<std::string, GLuint> textureCache;
    std::string textureError;

    auto acquireTexture = [&](const cgltf_texture_view& texView) -> GLuint {
        if (!texView.texture || !texView.texture->image) {
            return 0;
        }

        const cgltf_image* img = texView.texture->image;
        std::string key;
        if (img->uri) {
            key = (glbPath.parent_path() / img->uri).lexically_normal().string();
        } else {
            key = "embedded_" + std::to_string(reinterpret_cast<std::uintptr_t>(img));
        }

        auto it = textureCache.find(key);
        if (it != textureCache.end()) {
            return it->second;
        }

        GLuint tex = LoadTextureFromGltfImage(img, glbPath.parent_path(), textureError);
        if (tex != 0) {
            textureCache.emplace(key, tex);
            textures_.push_back(tex);
        }
        return tex;
    };

    // Helper to convert cgltf matrix to glm::mat4
    auto cgltfMatrixToGlm = [](const cgltf_float* m) -> glm::mat4 {
        return glm::mat4(
            m[0], m[1], m[2], m[3],
            m[4], m[5], m[6], m[7],
            m[8], m[9], m[10], m[11],
            m[12], m[13], m[14], m[15]
        );
    };

    // Traverse scene graph and process meshes with their transforms
    std::function<void(cgltf_node*, glm::mat4)> processNode = [&](cgltf_node* node, glm::mat4 parentTransform) {
        // Compute node's world transform
        glm::mat4 nodeTransform = parentTransform;
        if (node->has_matrix) {
            nodeTransform = parentTransform * cgltfMatrixToGlm(node->matrix);
        } else {
            // Build transform from TRS
            glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(node->translation[0], node->translation[1], node->translation[2]));
            glm::mat4 r = glm::mat4_cast(glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]));
            glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(node->scale[0], node->scale[1], node->scale[2]));
            nodeTransform = parentTransform * t * r * s;
        }

        // Process mesh if this node has one
        if (node->mesh) {
            cgltf_mesh& mesh = *node->mesh;
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
                const cgltf_primitive& prim = mesh.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
                prim.type != cgltf_primitive_type_triangle_fan) {
                continue;
            }

            const cgltf_accessor* posAcc = FindAttribute(prim, cgltf_attribute_type_position);
            if (!posAcc) {
                continue;
            }
            const cgltf_accessor* normAcc = FindAttribute(prim, cgltf_attribute_type_normal);
            const cgltf_accessor* uvAcc = FindAttribute(prim, cgltf_attribute_type_texcoord);
            const cgltf_accessor* idxAcc = prim.indices ? prim.indices : nullptr;
            if (!idxAcc) {
                continue;
            }

            const cgltf_size vertexCount = posAcc->count;
            if (vertexCount == 0 || idxAcc->count == 0) {
                continue;
            }

            std::size_t baseVertex = vertices.size();
            vertices.resize(baseVertex + vertexCount);

            // Read vertex attributes
            std::vector<float> posFloats(vertexCount * 3, 0.0f);
            
            cgltf_size unpackedCount = cgltf_accessor_unpack_floats(posAcc, posFloats.data(), posFloats.size());
            bool posOk = (unpackedCount == vertexCount * 3);
            if (!posOk && vertexCount > 0) {
                std::cerr << "Warning: Failed to unpack position data, unpacked=" << unpackedCount << " expected=" << (vertexCount * 3) << std::endl;
            }

            std::vector<float> normFloats;
            if (normAcc) {
                normFloats.resize(vertexCount * 3, 0.0f);
                cgltf_accessor_unpack_floats(normAcc, normFloats.data(), normFloats.size());
            }

            std::vector<float> uvFloats;
            if (uvAcc) {
                uvFloats.resize(vertexCount * 2, 0.0f);
                cgltf_accessor_unpack_floats(uvAcc, uvFloats.data(), uvFloats.size());
            }

            glm::vec3 primMin( std::numeric_limits<float>::max());
            glm::vec3 primMax(-std::numeric_limits<float>::max());

            for (cgltf_size vi = 0; vi < vertexCount; ++vi) {
                    VertexPNT v{};
                    if (posOk) {
                        glm::vec4 pos = nodeTransform * glm::vec4(posFloats[vi * 3 + 0],
                                                                   posFloats[vi * 3 + 1],
                                                                   posFloats[vi * 3 + 2],
                                                                   1.0f);
                        v.position = glm::vec3(pos.x, pos.y, pos.z);
                    primMin = glm::min(primMin, v.position);
                    primMax = glm::max(primMax, v.position);
                    }
                    if (!normFloats.empty()) {
                        // Transform normals using inverse transpose
                        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(nodeTransform)));
                        glm::vec3 norm = normalMatrix * glm::vec3(normFloats[vi * 3 + 0],
                                                                   normFloats[vi * 3 + 1],
                                                                   normFloats[vi * 3 + 2]);
                        v.normal = glm::normalize(norm);
                    }
                    if (!uvFloats.empty()) {
                        v.texCoord = glm::vec2(uvFloats[vi * 2 + 0],
                                               uvFloats[vi * 2 + 1]);
                    }
                    vertices[baseVertex + vi] = v;
                }

                // Indices
                cgltf_size idxCount = idxAcc->count;
                MeshDrawCall draw{};
                draw.startIndex = static_cast<uint32_t>(indices.size());
                draw.indexCount = static_cast<uint32_t>(idxCount);
            draw.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            draw.metallic = 0.0f;
            draw.roughness = 1.0f;

            if (prim.material) {
                const auto& mr = prim.material->pbr_metallic_roughness;
                draw.baseColorFactor = glm::vec4(mr.base_color_factor[0], mr.base_color_factor[1], mr.base_color_factor[2], mr.base_color_factor[3]);
                draw.metallic = mr.metallic_factor;
                draw.roughness = mr.roughness_factor;
                
                // Base color texture
                if (mr.base_color_texture.texture && mr.base_color_texture.texture->image) {
                    GLuint tex = acquireTexture(mr.base_color_texture);
                    if (tex != 0) {
                        draw.baseColorTexture = tex;
                        draw.hasBaseColorTexture = true;
                    }
                }
                // Metallic-roughness texture (G channel = roughness, B channel = metallic)
                if (mr.metallic_roughness_texture.texture && mr.metallic_roughness_texture.texture->image) {
                    GLuint tex = acquireTexture(mr.metallic_roughness_texture);
                    if (tex != 0) {
                        draw.metallicRoughnessTexture = tex;
                        draw.hasMetallicRoughnessTexture = true;
                    }
                }
            }

                for (cgltf_size i = 0; i < idxCount; ++i) {
                    cgltf_size rawIndex = cgltf_accessor_read_index(idxAcc, i);
                    if (rawIndex == static_cast<cgltf_size>(-1) || rawIndex >= vertexCount) {
                        continue;
                    }
                    indices.push_back(static_cast<uint32_t>(baseVertex + rawIndex));
                }

                draw.indexCount = static_cast<uint32_t>(indices.size()) - draw.startIndex;
                if (draw.indexCount > 0) {
                    draw.aabbMin = primMin;
                    draw.aabbMax = primMax;
                    draws_.push_back(draw);
                    colliderMins_.push_back(primMin);
                    colliderMaxs_.push_back(primMax);
                }
            }
        }

        // Recursively process children
        for (cgltf_size i = 0; i < node->children_count; ++i) {
            processNode(node->children[i], nodeTransform);
        }
    };

    // Get the active scene and traverse from root nodes
    const cgltf_scene* scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene) {
        glm::mat4 identity = glm::mat4(1.0f);
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) {
            processNode(scene->nodes[i], identity);
        }
    } else {
        // Fallback: process all meshes directly (no scene graph)
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            cgltf_mesh& mesh = data->meshes[m];
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
                const cgltf_primitive& prim = mesh.primitives[p];
                if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
                    prim.type != cgltf_primitive_type_triangle_fan) {
                    continue;
                }

                const cgltf_accessor* posAcc = FindAttribute(prim, cgltf_attribute_type_position);
                if (!posAcc) {
                    continue;
                }
                const cgltf_accessor* normAcc = FindAttribute(prim, cgltf_attribute_type_normal);
                const cgltf_accessor* uvAcc = FindAttribute(prim, cgltf_attribute_type_texcoord);
                const cgltf_accessor* idxAcc = prim.indices ? prim.indices : nullptr;
                if (!idxAcc) {
                    continue;
                }

                const cgltf_size vertexCount = posAcc->count;
                if (vertexCount == 0 || idxAcc->count == 0) {
                    continue;
                }

                std::size_t baseVertex = vertices.size();
                vertices.resize(baseVertex + vertexCount);

                std::vector<float> posFloats(vertexCount * 3, 0.0f);
                bool posOk = cgltf_accessor_unpack_floats(posAcc, posFloats.data(), posFloats.size()) == cgltf_result_success;

                std::vector<float> normFloats;
                if (normAcc) {
                    normFloats.resize(vertexCount * 3, 0.0f);
                    cgltf_accessor_unpack_floats(normAcc, normFloats.data(), normFloats.size());
                }

                std::vector<float> uvFloats;
                if (uvAcc) {
                    uvFloats.resize(vertexCount * 2, 0.0f);
                    cgltf_accessor_unpack_floats(uvAcc, uvFloats.data(), uvFloats.size());
                }

                glm::vec3 primMin( std::numeric_limits<float>::max());
                glm::vec3 primMax(-std::numeric_limits<float>::max());

                for (cgltf_size vi = 0; vi < vertexCount; ++vi) {
                    VertexPNT v{};
                    if (posOk) {
                        v.position = glm::vec3(posFloats[vi * 3 + 0],
                                               posFloats[vi * 3 + 1],
                                               posFloats[vi * 3 + 2]);
                        primMin = glm::min(primMin, v.position);
                        primMax = glm::max(primMax, v.position);
                    }
                    if (!normFloats.empty()) {
                        v.normal = glm::vec3(normFloats[vi * 3 + 0],
                                             normFloats[vi * 3 + 1],
                                             normFloats[vi * 3 + 2]);
                    }
                    if (!uvFloats.empty()) {
                        v.texCoord = glm::vec2(uvFloats[vi * 2 + 0],
                                               uvFloats[vi * 2 + 1]);
                    }
                    vertices[baseVertex + vi] = v;
                }

                cgltf_size idxCount = idxAcc->count;
                MeshDrawCall draw{};
                draw.startIndex = static_cast<uint32_t>(indices.size());
                draw.indexCount = static_cast<uint32_t>(idxCount);
                draw.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
                draw.metallic = 0.0f;
                draw.roughness = 1.0f;

                if (prim.material) {
                    const auto& mr = prim.material->pbr_metallic_roughness;
                    draw.baseColorFactor = glm::vec4(mr.base_color_factor[0], mr.base_color_factor[1], mr.base_color_factor[2], mr.base_color_factor[3]);
                    draw.metallic = mr.metallic_factor;
                    draw.roughness = mr.roughness_factor;
                    if (mr.base_color_texture.texture) {
                        GLuint tex = acquireTexture(mr.base_color_texture);
                        if (tex != 0) {
                            draw.baseColorTexture = tex;
                            draw.hasBaseColorTexture = true;
                        }
                    }
                    if (mr.metallic_roughness_texture.texture) {
                        GLuint tex = acquireTexture(mr.metallic_roughness_texture);
                        if (tex != 0) {
                            draw.metallicRoughnessTexture = tex;
                            draw.hasMetallicRoughnessTexture = true;
                        }
                    }
                }

                for (cgltf_size i = 0; i < idxCount; ++i) {
                    cgltf_size rawIndex = cgltf_accessor_read_index(idxAcc, i);
                    if (rawIndex == static_cast<cgltf_size>(-1) || rawIndex >= vertexCount) {
                        continue;
                    }
                    indices.push_back(static_cast<uint32_t>(baseVertex + rawIndex));
                }

                draw.indexCount = static_cast<uint32_t>(indices.size()) - draw.startIndex;
                if (draw.indexCount > 0) {
                    draw.aabbMin = primMin;
                    draw.aabbMax = primMax;
                    draws_.push_back(draw);
                    colliderMins_.push_back(primMin);
                    colliderMaxs_.push_back(primMax);
                }
            }
        }
    }

    cgltf_free(data);

    if (vertices.empty() || indices.empty()) {
        if (errorMessage) {
            *errorMessage = "GLB contained no drawable primitives.";
        }
        Destroy();
        return false;
    }

    // Build per-triangle colliders for tighter collision
    colliderMins_.clear();
    colliderMaxs_.clear();
    colliderMins_.reserve(indices.size() / 3);
    colliderMaxs_.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3& p0 = vertices[indices[i]].position;
        const glm::vec3& p1 = vertices[indices[i + 1]].position;
        const glm::vec3& p2 = vertices[indices[i + 2]].position;
        glm::vec3 mn = glm::min(p0, glm::min(p1, p2));
        glm::vec3 mx = glm::max(p0, glm::max(p1, p2));
        colliderMins_.push_back(mn);
        colliderMaxs_.push_back(mx);
    }

    boundsMin_ = vertices.front().position;
    boundsMax_ = vertices.front().position;
    for (const auto& v : vertices) {
        boundsMin_ = glm::min(boundsMin_, v.position);
        boundsMax_ = glm::max(boundsMax_, v.position);
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(VertexPNT),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(uint32_t),
                 indices.data(),
                 GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(VertexPNT);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, texCoord)));

    glBindVertexArray(0);

    // Generate normals if missing
    bool hasNormals = false;
    for (const auto& v : vertices) {
        if (glm::dot(v.normal, v.normal) > 0.0f) {
            hasNormals = true;
            break;
        }
    }
    if (!hasNormals) {
        std::vector<VertexPNT> verts = vertices;
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            VertexPNT& a = verts[indices[i]];
            VertexPNT& b = verts[indices[i + 1]];
            VertexPNT& c = verts[indices[i + 2]];
            glm::vec3 ab = b.position - a.position;
            glm::vec3 ac = c.position - a.position;
            glm::vec3 normal = glm::normalize(glm::cross(ab, ac));
            if (std::isnan(normal.x) || std::isnan(normal.y) || std::isnan(normal.z)) {
                continue;
            }
            a.normal += normal;
            b.normal += normal;
            c.normal += normal;
        }
        for (auto& v : verts) {
            if (glm::dot(v.normal, v.normal) > 0.0f) {
                v.normal = glm::normalize(v.normal);
            } else {
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(VertexPNT), verts.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // Build per-triangle colliders
    colliderMins_.reserve(indices.size() / 3);
    colliderMaxs_.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3& p0 = vertices[indices[i]].position;
        const glm::vec3& p1 = vertices[indices[i + 1]].position;
        const glm::vec3& p2 = vertices[indices[i + 2]].position;
        glm::vec3 mn = glm::min(p0, glm::min(p1, p2));
        glm::vec3 mx = glm::max(p0, glm::max(p1, p2));
        colliderMins_.push_back(mn);
        colliderMaxs_.push_back(mx);
    }

    indexCount_ = indices.size();
    std::cerr << "Loaded GLB '" << glbPath.string() << "' vertices=" << vertices.size()
              << " indices=" << indices.size()
              << " boundsMin=(" << boundsMin_.x << "," << boundsMin_.y << "," << boundsMin_.z << ")"
              << " boundsMax=(" << boundsMax_.x << "," << boundsMax_.y << "," << boundsMax_.z << ")"
              << std::endl;
    return true;
}

bool Model::LoadFromFbx(const std::filesystem::path& fbxPath, std::string* errorMessage) {
    Assimp::Importer importer;
    // FBX files often use centimeters, scale to meters (divide by 100)
    // Try setting global scale factor - if this doesn't work, we'll scale manually
    importer.SetPropertyFloat("GLOBAL_SCALE_FACTOR", 0.01f);
    const aiScene* scene = importer.ReadFile(
        fbxPath.string().c_str(),
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        if (errorMessage) {
            *errorMessage = "Failed to load FBX: " + std::string(importer.GetErrorString());
        }
        return false;
    }

    if (scene->mNumMeshes == 0) {
        if (errorMessage) {
            *errorMessage = "FBX file contains no meshes.";
        }
        return false;
    }

    Destroy();

    std::vector<VertexPNT> vertices;
    std::vector<uint32_t> indices;
    draws_.clear();
    textures_.clear();

    std::unordered_map<std::string, GLuint> textureCache;
    std::string textureError;
    
    // Open log file for texture loading
    std::ofstream textureLog("texture_load_log.txt");
    if (textureLog.is_open()) {
        textureLog << "Texture Loading Log for: " << fbxPath.string() << "\n";
        textureLog << "========================================\n\n";
    }

    auto acquireTexture = [&](const std::filesystem::path& texPath, bool silent = false) -> GLuint {
        auto canonical = texPath.lexically_normal();
        std::string key = canonical.string();
        auto cacheIt = textureCache.find(key);
        if (cacheIt != textureCache.end()) {
            return cacheIt->second;
        }

        GLuint texture = 0;
        std::string loadError;
        if (gfx::LoadTexture2D(canonical, texture, &loadError)) {
            textureCache.emplace(key, texture);
            textures_.push_back(texture);
            if (!silent && textureLog.is_open()) {
                textureLog << "✓ Loaded: " << canonical.filename().string() << " from " << canonical.string() << "\n";
            }
            static int successCount = 0;
            if (successCount < 30) {
                std::cout << "✓ Loaded texture: " << canonical.filename().string() << std::endl;
                successCount++;
            }
            return texture;
        }
        return 0;
    };

    // Helper function to convert aiMatrix4x4 to glm::mat4
    auto aiMatrixToGlm = [](const aiMatrix4x4& aiMat) -> glm::mat4 {
        glm::mat4 result;
        result[0][0] = aiMat.a1; result[0][1] = aiMat.b1; result[0][2] = aiMat.c1; result[0][3] = aiMat.d1;
        result[1][0] = aiMat.a2; result[1][1] = aiMat.b2; result[1][2] = aiMat.c2; result[1][3] = aiMat.d2;
        result[2][0] = aiMat.a3; result[2][1] = aiMat.b3; result[2][2] = aiMat.c3; result[2][3] = aiMat.d3;
        result[3][0] = aiMat.a4; result[3][1] = aiMat.b4; result[3][2] = aiMat.c4; result[3][3] = aiMat.d4;
        return result;
    };

    uint32_t baseIndex = 0;

    // Traverse scene graph and process meshes with their transforms
    std::function<void(aiNode*, glm::mat4)> processNode = [&](aiNode* node, glm::mat4 parentTransform) {
        glm::mat4 nodeTransform = parentTransform * aiMatrixToGlm(node->mTransformation);

        // Process all meshes in this node
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            unsigned int meshIndex = node->mMeshes[i];
            const aiMesh* mesh = scene->mMeshes[meshIndex];
            if (!mesh->HasPositions()) {
                continue;
            }

            uint32_t meshStartIndex = static_cast<uint32_t>(indices.size());
            uint32_t meshBaseIndex = baseIndex;

            // Transform vertices with node transform
            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                VertexPNT vertex;
                aiVector3D pos = mesh->mVertices[v];
                glm::vec4 posTransformed = nodeTransform * glm::vec4(pos.x, pos.y, pos.z, 1.0f);
                vertex.position = glm::vec3(posTransformed.x, posTransformed.y, posTransformed.z);

                // Transform normals (use inverse transpose for proper normal transformation)
                glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(nodeTransform)));
                if (mesh->HasNormals()) {
                    aiVector3D norm = mesh->mNormals[v];
                    glm::vec3 normalTransformed = normalMatrix * glm::vec3(norm.x, norm.y, norm.z);
                    vertex.normal = glm::normalize(normalTransformed);
                } else {
                    vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }

                vertex.texCoord = (mesh->HasTextureCoords(0))
                    ? glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y)
                    : glm::vec2(0.0f);
                vertices.push_back(vertex);
            }

            for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices == 3) {
                    for (unsigned int j = 0; j < 3; ++j) {
                        indices.push_back(meshBaseIndex + face.mIndices[j]);
                    }
                }
            }

            MeshDrawCall draw;
            draw.startIndex = meshStartIndex;
            draw.indexCount = static_cast<uint32_t>(indices.size() - meshStartIndex);

            if (mesh->mMaterialIndex < scene->mNumMaterials) {
                const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
                // Default values
                draw.baseColorFactor = glm::vec4(1.0f);
                draw.metallic = 0.0f;
                draw.roughness = 1.0f;
                aiColor3D diffuse(0.8f, 0.8f, 0.8f);
                material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
                draw.baseColorFactor = glm::vec4(diffuse.r, diffuse.g, diffuse.b, 1.0f);

                if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
                    aiString texPath;
                    if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                        std::string texStr = std::string(texPath.C_Str());
                        // Extract just the filename (handle Windows paths with backslashes)
                        // Replace backslashes with forward slashes first
                        std::replace(texStr.begin(), texStr.end(), '\\', '/');
                        std::filesystem::path texPathObj(texStr);
                        std::string filename = texPathObj.filename().string();
                        
                        // Debug: log what texture we're looking for
                        static std::set<std::string> searchedTextures;
                        if (searchedTextures.insert(filename).second) {
                            if (textureLog.is_open()) {
                                textureLog << "\nMaterial requested texture: " << filename << "\n";
                                textureLog << "  Original path in FBX: " << texStr << "\n";
                            }
                        }
                        
                        // Also try with .jpeg extension if original was .jpg
                        std::string filenameJpeg = filename;
                        if (filenameJpeg.size() > 4 && filenameJpeg.substr(filenameJpeg.size() - 4) == ".jpg") {
                            filenameJpeg = filenameJpeg.substr(0, filenameJpeg.size() - 4) + ".jpeg";
                        }
                        
                        // Try multiple locations
                        std::vector<std::filesystem::path> searchPaths = {
                            fbxPath.parent_path() / texStr,  // Original relative path
                            fbxPath.parent_path() / filename,  // Just filename in source dir
                            fbxPath.parent_path() / filenameJpeg,  // Filename with .jpeg extension
                            fbxPath.parent_path().parent_path() / "textures" / texStr,  // Full path in textures
                            fbxPath.parent_path().parent_path() / "textures" / filename,  // Just filename in textures
                            fbxPath.parent_path().parent_path() / "textures" / filenameJpeg,  // Filename with .jpeg in textures
                        };
                        
                        GLuint tex = 0;
                        std::filesystem::path loadedPath;
                        for (const auto& searchPath : searchPaths) {
                            tex = acquireTexture(searchPath, true);  // Silent for intermediate attempts
                            if (tex != 0) {
                                loadedPath = searchPath;
                                break;
                            }
                        }
                        
                        if (tex != 0) {
                            draw.baseColorTexture = tex;
                            draw.hasBaseColorTexture = true;
                            if (textureLog.is_open()) {
                                textureLog << "  → SUCCESS: Found at " << loadedPath.string() << "\n";
                            }
                        } else {
                            if (textureLog.is_open()) {
                                textureLog << "  → FAILED: Tried " << searchPaths.size() << " locations:\n";
                                for (const auto& path : searchPaths) {
                                    textureLog << "    - " << path.string() << (std::filesystem::exists(path) ? " (exists but failed to load)" : " (not found)") << "\n";
                                }
                            }
                        }
                    }
                }
            }

            draws_.push_back(draw);
            baseIndex += mesh->mNumVertices;
        }

        // Recursively process child nodes
        for (unsigned int i = 0; i < node->mNumChildren; ++i) {
            processNode(node->mChildren[i], nodeTransform);
        }
    };

    // Start processing from root node
    processNode(scene->mRootNode, glm::mat4(1.0f));

    if (vertices.empty() || indices.empty()) {
        if (errorMessage) {
            *errorMessage = "FBX contained no drawable geometry.";
        }
        return false;
    }

    // Scale down if model is too large (FBX files often come in centimeters)
    float maxExtent = 0.0f;
    if (!vertices.empty()) {
        boundsMin_ = vertices.front().position;
        boundsMax_ = vertices.front().position;
        for (const auto& v : vertices) {
            boundsMin_ = glm::min(boundsMin_, v.position);
            boundsMax_ = glm::max(boundsMax_, v.position);
        }
        glm::vec3 extent = boundsMax_ - boundsMin_;
        maxExtent = std::max({extent.x, extent.y, extent.z});
        
        // If model is > 100 units, scale it down (likely centimeters)
        if (maxExtent > 100.0f) {
            float scale = 0.01f; // Convert cm to m
            std::cout << "Model is very large (max extent: " << maxExtent << "), scaling by " << scale << std::endl;
            for (auto& v : vertices) {
                v.position *= scale;
            }
            boundsMin_ *= scale;
            boundsMax_ *= scale;
        }
    } else {
        boundsMin_ = glm::vec3(0.0f);
        boundsMax_ = glm::vec3(0.0f);
    }

    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(VertexPNT), vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &ebo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = sizeof(VertexPNT);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(VertexPNT, texCoord)));

    glBindVertexArray(0);

    indexCount_ = indices.size();
    std::cerr << "Loaded FBX '" << fbxPath.string() << "' vertices=" << vertices.size()
              << " indices=" << indices.size() << " boundsMin=(" << boundsMin_.x << "," << boundsMin_.y << "," << boundsMin_.z
              << ") boundsMax=(" << boundsMax_.x << "," << boundsMax_.y << "," << boundsMax_.z << ")\n";
    
    if (textureLog.is_open()) {
        textureLog << "\n========================================\n";
        textureLog << "Summary: " << textureCache.size() << " unique textures loaded\n";
        textureLog << "Total draw calls: " << draws_.size() << "\n";
        textureLog.close();
        std::cout << "Texture loading log written to texture_load_log.txt\n";
    }
    
    return true;
}

void Model::Draw(const ShaderProgram& shader) const {
    if (vao_ == 0 || indexCount_ == 0) {
        return;
    }

    glBindVertexArray(vao_);
    for (const auto& draw : draws_) {
        shader.SetVec4("uMaterial.baseColorFactor", draw.baseColorFactor);
        shader.SetFloat("uMaterial.metallic", draw.metallic);
        shader.SetFloat("uMaterial.roughness", draw.roughness);
        shader.SetInt("uMaterial.hasBaseColorTex", draw.hasBaseColorTexture ? 1 : 0);
        shader.SetInt("uMaterial.hasMetalRoughTex", draw.hasMetallicRoughnessTexture ? 1 : 0);
        if (draw.hasBaseColorTexture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, draw.baseColorTexture);
        }
        if (draw.hasMetallicRoughnessTexture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, draw.metallicRoughnessTexture);
        }

        const void* offsetPtr = reinterpret_cast<const void*>(static_cast<uintptr_t>(draw.startIndex) * sizeof(uint32_t));
        glDrawElements(GL_TRIANGLES, draw.indexCount, GL_UNSIGNED_INT, offsetPtr);

        if (draw.hasMetallicRoughnessTexture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (draw.hasBaseColorTexture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    glBindVertexArray(0);
}

void Model::Destroy() {
    for (GLuint tex : textures_) {
        glDeleteTextures(1, &tex);
    }
    textures_.clear();
    draws_.clear();
    indexCount_ = 0;
    boundsMin_ = glm::vec3(0.0f);
    boundsMax_ = glm::vec3(0.0f);

    if (ebo_ != 0) {
        glDeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}


