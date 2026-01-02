#include "Model.hpp"
#include "ShaderProgram.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

// CameraController holds orbit camera state (distance, yaw/pitch in radians, drag flag, last mouse pos).
struct CameraController {
    float distance = 160.0f;
    float yaw = glm::radians(45.0f);
    float pitch = glm::radians(12.0f);
    bool dragging = false;
    double lastX = 0.0;
    double lastY = 0.0;
};

// GLFW error callback: logs error code/message.
void ErrorCallback(int code, const char* description) {
    std::cerr << "[GLFW] Error " << code << ": " << description << std::endl;
}

// GLFW resize callback: updates the GL viewport.
void FrameBufferSizeCallback(GLFWwindow* /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

// Initializes GLFW with core profile hints. Returns true on success.
bool InitGLFW() {
    glfwSetErrorCallback(ErrorCallback);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW." << std::endl;
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);
    return true;
}

// Initializes GLEW. Returns true on success.
bool InitGLEW() {
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    // GLEW can emit a benign error on init; clear it.
    glGetError();
    if (glewError != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW: "
                  << reinterpret_cast<const char*>(glewGetErrorString(glewError)) << std::endl;
        return false;
    }
    return true;
}

// Helper to fetch camera state stored in the window user pointer.
CameraController* GetCamera(GLFWwindow* window) {
    return static_cast<CameraController*>(glfwGetWindowUserPointer(window));
}

// Mouse wheel callback: zooms camera in/out.
void ScrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
    if (auto* camera = GetCamera(window)) {
        camera->distance -= static_cast<float>(yoffset) * 8.0f;
        camera->distance = std::clamp(camera->distance, 20.0f, 400.0f);
    }
}

// Mouse button callback: starts/stops dragging for orbit camera.
void MouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }
    auto* camera = GetCamera(window);
    if (!camera) {
        return;
    }

    if (action == GLFW_PRESS) {
        camera->dragging = true;
        glfwGetCursorPos(window, &camera->lastX, &camera->lastY);
    } else if (action == GLFW_RELEASE) {
        camera->dragging = false;
    }
}

// Mouse move callback: updates yaw/pitch when dragging.
void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* camera = GetCamera(window);
    if (!camera || !camera->dragging) {
        return;
    }

    double dx = xpos - camera->lastX;
    double dy = ypos - camera->lastY;
    camera->lastX = xpos;
    camera->lastY = ypos;

    camera->yaw -= static_cast<float>(dx) * 0.005f;
    camera->pitch -= static_cast<float>(dy) * 0.005f;
    camera->pitch = std::clamp(camera->pitch, -1.2f, 1.2f);
}

// Keyboard camera controls (orbit and zoom) when enabled. deltaTime is frame time in seconds.
void UpdateCameraFromKeyboard(GLFWwindow* window, CameraController& camera, float deltaTime) {
    const float orbitSpeed = 1.5f;
    const float zoomSpeed = 120.0f;

    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        camera.yaw -= orbitSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        camera.yaw += orbitSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        camera.pitch -= orbitSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        camera.pitch += orbitSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        camera.distance += zoomSpeed * deltaTime;
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        camera.distance -= zoomSpeed * deltaTime;
    }

    camera.pitch = std::clamp(camera.pitch, -1.2f, 1.2f);
    camera.distance = std::clamp(camera.distance, 20.0f, 400.0f);
}

// Tries several candidate paths to locate the assets directory. Returns the first existing dir.
std::filesystem::path FindAssetsRoot(const char* executablePath) {
    std::vector<std::filesystem::path> candidates;

    if (const char* overrideEnv = std::getenv("ASSETS_ROOT")) {
        if (*overrideEnv) {
            candidates.emplace_back(overrideEnv);
        }
    }

    if (executablePath) {
        std::error_code ec;
        auto exePath = std::filesystem::weakly_canonical(std::filesystem::path(executablePath), ec);
        if (!ec) {
            candidates.push_back(exePath.parent_path() / "assets");
        }
    }

    candidates.push_back(std::filesystem::current_path() / "assets");
    candidates.push_back(std::filesystem::path(PROJECT_SOURCE_DIR) / "assets");

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
            return candidate;
        }
    }

    return std::filesystem::path(PROJECT_SOURCE_DIR) / "assets";
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;

    if (!InitGLFW()) {
        return EXIT_FAILURE;
    }

    const int initialWidth = 1280;
    const int initialHeight = 720;
    GLFWwindow* window = glfwCreateWindow(initialWidth, initialHeight, "Super Mario 3D - Boilerplate", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window." << std::endl;
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, FrameBufferSizeCallback);
    glfwSwapInterval(1);

    if (!InitGLEW()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    CameraController camera;
    glfwSetWindowUserPointer(window, &camera);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);

    std::cout << "Controls: drag with LMB to orbit, scroll/Q/E to zoom, WASD/arrow keys to adjust view.\n";

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    // Don't use framebuffer sRGB - we handle gamma correction manually in shader

    auto assetsRoot = FindAssetsRoot(argv ? argv[0] : nullptr);
    const std::filesystem::path shaderRoot = assetsRoot / "shaders";
    ShaderProgram shaderProgram;
    std::string shaderError;
    if (!shaderProgram.LoadFromFiles(shaderRoot / "pbr.vert", shaderRoot / "pbr.frag", &shaderError)) {
        std::cerr << shaderError << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // Try to load a level file (prefer GLTF/GLB, then FBX, then OBJ).
    Model sceneModel;
    std::string modelError;
    const std::filesystem::path gltfPath = assetsRoot / "models" / "super_mario_bros._level_1_-_1" / "scene.gltf";
    const std::filesystem::path glbPath = assetsRoot / "models" / "super_mario_bros._level_1_-_1" / "scene.glb";
    const std::filesystem::path fbxPath = assetsRoot / "models" / "tanabata-evening-kyoto-inspired-city-scene" / "source" / "testexport.fbx";
    const std::filesystem::path objPath = assetsRoot / "models" / "map.obj";
    bool loaded = false;
    // Try GLTF first (supports both .gltf and .glb)
    if (std::filesystem::exists(gltfPath)) {
        loaded = sceneModel.LoadFromGlb(gltfPath, &modelError);  // LoadFromGlb handles both .gltf and .glb
    }
    if (!loaded && std::filesystem::exists(glbPath)) {
        loaded = sceneModel.LoadFromGlb(glbPath, &modelError);
    }
    // Fallback to FBX
    if (!loaded && std::filesystem::exists(fbxPath)) {
        loaded = sceneModel.LoadFromFbx(fbxPath, &modelError);
    }
    // Fallback to OBJ
    if (!loaded && std::filesystem::exists(objPath)) {
        loaded = sceneModel.LoadFromObj(objPath, &modelError);
    }
    if (!loaded) {
        std::cerr << "Failed to load level: " << modelError << std::endl;
        std::cerr << "Tried: " << gltfPath.string() << ", " << glbPath.string() << ", " << fbxPath.string() << ", " << objPath.string() << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glm::vec3 boundsMin = sceneModel.GetBoundsMin();
    glm::vec3 boundsMax = sceneModel.GetBoundsMax();
    glm::vec3 target = 0.5f * (boundsMin + boundsMax);
    glm::vec3 extent = boundsMax - boundsMin;
    float diag = glm::length(extent);
    if (diag <= 0.001f) {
        diag = 50.0f;
    }
    // For very large models, use a larger distance multiplier
    camera.distance = diag * 1.5f;
    std::cout << "Model bounds: min=" << boundsMin.x << "," << boundsMin.y << "," << boundsMin.z 
              << " max=" << boundsMax.x << "," << boundsMax.y << "," << boundsMax.z 
              << " diagonal=" << diag << " camera distance=" << camera.distance << std::endl;
    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    glm::vec3 lightColor(1.0f, 0.96f, 0.86f);
    // Moderate ambient lighting
    glm::vec3 ambientColor(0.15f, 0.15f, 0.18f);

    float previousTime = static_cast<float>(glfwGetTime());

    // Player setup
    struct Player {
        glm::vec3 pos{0.0f};
        glm::vec3 vel{0.0f};
        glm::vec3 halfExtents{0.5f, 1.0f, 0.5f};
        bool grounded = false;
    } player;
    // Spawn above level
    float levelMidZ = 0.5f * (boundsMin.z + boundsMax.z);
    player.pos = glm::vec3(target.x, boundsMax.y + 5.0f, levelMidZ);

    const auto& colMins = sceneModel.GetColliderMins();
    const auto& colMaxs = sceneModel.GetColliderMaxs();

    // AABB overlap test utility.
    auto aabbOverlap = [](const glm::vec3& minA, const glm::vec3& maxA, const glm::vec3& minB, const glm::vec3& maxB) {
        return (minA.x <= maxB.x && maxA.x >= minB.x) &&
               (minA.y <= maxB.y && maxA.y >= minB.y) &&
               (minA.z <= maxB.z && maxA.z >= minB.z);
    };

    // Skin width for collision resolution
    float SKIN = 0.02f;
    // Maximum penetration allowed before resolving
    float MAX_PENETRATION = 0.1f;
    // Per-axis swept movement with collision resolution against static colliders.
    // Inputs: pos (player center), vel (player velocity), dt (delta time), axis (0=X,1=Y,2=Z).
    // Output: pos/vel are modified in-place to resolve collisions.
    auto moveAxis = [&](glm::vec3& pos, glm::vec3& vel, float dt, int axis) {
        float delta = vel[axis] * dt;
        if (delta == 0.0f) return;
    
        float dir = (delta > 0.0f) ? 1.0f : -1.0f;
        float move = delta;
    
        glm::vec3 pMin = pos - player.halfExtents;
        glm::vec3 pMax = pos + player.halfExtents;
    
        float bestMove = move;
    
        for (size_t i = 0; i < colMins.size(); ++i) {
            const glm::vec3& cMin = colMins[i];
            const glm::vec3& cMax = colMaxs[i];
    
            // Overlap on other axes
            if (axis == 0) {
                if (pMax.y <= cMin.y || pMin.y >= cMax.y) continue;
                if (pMax.z <= cMin.z || pMin.z >= cMax.z) continue;
            } else if (axis == 1) {
                if (pMax.x <= cMin.x || pMin.x >= cMax.x) continue;
                if (pMax.z <= cMin.z || pMin.z >= cMax.z) continue;
            }
    
            float dist;
            if (dir > 0.0f) {
                dist = cMin[axis] - pMax[axis] - SKIN;
            } else {
                dist = cMax[axis] - pMin[axis] + SKIN;
            }
    
            // Handle initial penetration (important for floors)
            if (std::abs(dist) < MAX_PENETRATION) {
                // Push OUT of collision, not deeper
                if (dir < 0.0f) { // falling
                    bestMove = std::max(bestMove, dist);
                } else { // moving up
                    bestMove = std::min(bestMove, dist);
                }
                continue;
            }

    
            if ((dir > 0.0f && dist >= 0.0f && dist < bestMove) ||
                (dir < 0.0f && dist <= 0.0f && dist > bestMove)) {
                bestMove = dist;
            }
        }
    
        pos[axis] += bestMove;
    
        if (bestMove != move) {
            vel[axis] = 0.0f;
        }
    };
    
    

    // Simple cube for player render
    GLuint playerVAO = 0, playerVBO = 0;
    {
        float verts[] = {
            // pos               // normal        // uv
            -1,-1,-1, 0,0,-1, 0,0,
             1,-1,-1, 0,0,-1, 1,0,
             1, 1,-1, 0,0,-1, 1,1,
            -1, 1,-1, 0,0,-1, 0,1,
            -1,-1, 1, 0,0, 1, 0,0,
             1,-1, 1, 0,0, 1, 1,0,
             1, 1, 1, 0,0, 1, 1,1,
            -1, 1, 1, 0,0, 1, 0,1,
        };
        uint32_t inds[] = {
            0,1,2, 0,2,3,
            4,5,6, 4,6,7,
            0,1,5, 0,5,4,
            2,3,7, 2,7,6,
            0,3,7, 0,7,4,
            1,2,6, 1,6,5
        };
        glGenVertexArrays(1, &playerVAO);
        glBindVertexArray(playerVAO);
        glGenBuffers(1, &playerVBO);
        glBindBuffer(GL_ARRAY_BUFFER, playerVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        GLuint ebo;
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(inds), inds, GL_STATIC_DRAW);
        GLsizei stride = sizeof(float) * 8;
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float)*3));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float)*6));
        glBindVertexArray(0);
    }

    float moveSpeed = 25.0f;
    float jumpSpeed = 20.0f;
    float gravity = 50.0f;

    while (!glfwWindowShouldClose(window)) {
        float currentTime = static_cast<float>(glfwGetTime());
        float deltaTime = currentTime - previousTime;
        previousTime = currentTime;

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = width > 0 && height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;

        glViewport(0, 0, width, height);
        glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // Enable alpha blending for transparent objects (like foliage)
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);

        // Side-view camera locked to player.
        glm::vec3 cameraPos = player.pos + glm::vec3(0.0f, 10.0f, 25.0f); // side view along Z
        glm::vec3 lookTarget = player.pos;
        glm::mat4 view = glm::lookAt(cameraPos, lookTarget, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 500.0f);

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));

        // Player input & physics (side-scroller: lock Z, only X movement)
        glm::vec3 move(0.0f);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) move.x -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) move.x += 1.0f;
        if (glm::length(move) > 0.0f) move = glm::normalize(move);
        player.vel.x = move.x * moveSpeed;
        player.vel.z = 0.0f; // lock Z plane
        player.vel.y -= gravity * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && player.grounded) {
            player.vel.y = jumpSpeed;
            player.grounded = false;
        }
        // Z locked, no moveAxis on Z
        moveAxis(player.pos, player.vel, deltaTime, 1);
        // Integrate with per-axis collision
        moveAxis(player.pos, player.vel, deltaTime, 0);
        // Keep Z locked to plane
        player.pos.z = levelMidZ;
        // grounded check
        player.grounded = false;
        glm::vec3 pMin = player.pos - player.halfExtents;
        glm::vec3 pMax = player.pos + player.halfExtents;

        for (size_t i = 0; i < colMins.size(); ++i) {
            const auto& cMin = colMins[i];
            const auto& cMax = colMaxs[i];

            bool overlapXZ =
                pMax.x > cMin.x && pMin.x < cMax.x &&
                pMax.z > cMin.z && pMin.z < cMax.z;

                bool touchingFromAbove =
                std::abs(pMin.y - cMax.y) < 0.05f &&
                player.vel.y <= 0.0f;
            

            if (overlapXZ && touchingFromAbove) {
                player.grounded = true;
                break;
            }
        }

        shaderProgram.Use();
        shaderProgram.SetMat4("uModel", model);
        shaderProgram.SetMat4("uView", view);
        shaderProgram.SetMat4("uProjection", projection);
        shaderProgram.SetMat3("uNormalMatrix", normalMatrix);
        shaderProgram.SetVec3("uLightDir", lightDir);
        shaderProgram.SetVec3("uLightColor", lightColor);
        shaderProgram.SetVec3("uAmbientColor", ambientColor);
        shaderProgram.SetVec3("uCameraPos", cameraPos);
        shaderProgram.SetInt("uBaseColorMap", 0);
        shaderProgram.SetInt("uMetalRoughMap", 1);

        sceneModel.Draw(shaderProgram);

        // Draw player cube
        glm::mat4 playerModel = glm::translate(glm::mat4(1.0f), player.pos) * glm::scale(glm::mat4(1.0f), player.halfExtents);
        glm::mat3 playerNormal = glm::mat3(glm::transpose(glm::inverse(playerModel)));
        shaderProgram.SetMat4("uModel", playerModel);
        shaderProgram.SetMat3("uNormalMatrix", playerNormal);
        shaderProgram.SetVec4("uMaterial.baseColorFactor", glm::vec4(0.2f, 0.8f, 0.3f, 1.0f));
        shaderProgram.SetFloat("uMaterial.metallic", 0.0f);
        shaderProgram.SetFloat("uMaterial.roughness", 1.0f);
        shaderProgram.SetInt("uMaterial.hasBaseColorTex", 0);
        shaderProgram.SetInt("uMaterial.hasMetalRoughTex", 0);
        glBindVertexArray(playerVAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    sceneModel.Destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}


