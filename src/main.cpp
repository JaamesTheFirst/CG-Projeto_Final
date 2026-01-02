#include "Model.hpp"
#include "ShaderProgram.hpp"
#include "Player.hpp"
#include "Enemy.hpp"

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
#include <limits>
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

    // Discover available levels (directories under assets/models/levels).
    Model sceneModel;
    std::string modelError;
    std::vector<std::filesystem::path> levelRoots;
    const auto levelsRoot = assetsRoot / "models" / "levels";
    if (std::filesystem::exists(levelsRoot)) {
        for (const auto& entry : std::filesystem::directory_iterator(levelsRoot)) {
            if (entry.is_directory()) {
                levelRoots.push_back(entry.path());
            }
        }
        std::sort(levelRoots.begin(), levelRoots.end(), [](const auto& a, const auto& b) {
            return a.filename().string() < b.filename().string();
        });
        // Ensure the classic level_1 stays first if present.
        auto it = std::find_if(levelRoots.begin(), levelRoots.end(), [](const auto& p) {
            const auto name = p.filename().string();
            return name.find("level_1") != std::string::npos || name.find("super_mario") != std::string::npos;
        });
        if (it != levelRoots.end()) {
            std::rotate(levelRoots.begin(), it, it + 1);
        }
    }
    // Legacy fallback if no subdirs are found.
    if (levelRoots.empty()) {
        levelRoots.push_back(assetsRoot / "models" / "super_mario_bros._level_1_-_1");
    }

    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f}, target{0.0f}, extent{0.0f};
    float diag = 50.0f;
    float levelMidZ = 0.0f;
    glm::vec3 flagTriggerCenter{0.0f};
    float flagTriggerRadius = 10.0f;
    int currentLevel = 0;
    const std::filesystem::path fbxPath = assetsRoot / "models" / "tanabata-evening-kyoto-inspired-city-scene" / "source" / "testexport.fbx";
    const std::filesystem::path objPath = assetsRoot / "models" / "map.obj";
    const std::vector<glm::vec3>* colMins = nullptr;
    const std::vector<glm::vec3>* colMaxs = nullptr;

    Player player;
    std::vector<Enemy> enemies;

    auto updateDerivedBounds = [&]() {
        boundsMin = sceneModel.GetBoundsMin();
        boundsMax = sceneModel.GetBoundsMax();
        target = 0.5f * (boundsMin + boundsMax);
        extent = boundsMax - boundsMin;
        diag = glm::length(extent);
        if (diag <= 0.001f) {
            diag = 50.0f;
        }
        levelMidZ = 0.5f * (boundsMin.z + boundsMax.z);
        camera.distance = diag * 1.5f;
        std::cout << "Model bounds: min=" << boundsMin.x << "," << boundsMin.y << "," << boundsMin.z 
                  << " max=" << boundsMax.x << "," << boundsMax.y << "," << boundsMax.z 
                  << " diagonal=" << diag << " camera distance=" << camera.distance << std::endl;
        // Simple trigger volume near level end; when player overlaps, we advance to next level.
        // Spherical trigger near the end of the level (generous radius so it is easy to hit).
        float triggerDepth = std::max(5.0f, extent.x * 0.05f);
        flagTriggerCenter = glm::vec3(boundsMax.x - triggerDepth * 0.5f,
                                      0.5f * (boundsMin.y + boundsMax.y),
                                      levelMidZ);
        flagTriggerRadius = std::max({10.0f, triggerDepth * 1.5f, extent.z * 0.5f});
    };

    auto applyColliderRefs = [&]() {
        colMins = &sceneModel.GetColliderMins();
        colMaxs = &sceneModel.GetColliderMaxs();
    };

    auto findGroundBelow = [&](float x, float z, float maxY) -> float {
        float floorY = boundsMin.y;
        if (colMins && colMaxs) {
            for (size_t i = 0; i < colMins->size(); ++i) {
                const auto& cMin = (*colMins)[i];
                const auto& cMax = (*colMaxs)[i];
                if (x < cMin.x || x > cMax.x) continue;
                if (z < cMin.z || z > cMax.z) continue;
                if (cMax.y <= maxY + 0.001f) {
                    floorY = std::max(floorY, cMax.y);
                }
            }
        }
        return floorY;
    };

    auto spawnPlayerAtLevelStart = [&]() {
        player.vel = glm::vec3(0.0f);
        player.grounded = false;
        // Default spawn near the left edge, above the level.
        float desiredX = boundsMin.x + player.halfExtents.x + 1.0f;
        float fallbackY = boundsMax.y + 5.0f;
        float groundY = findGroundBelow(desiredX, levelMidZ, fallbackY);
        glm::vec3 spawnPos(desiredX, groundY + player.halfExtents.y + 0.05f, levelMidZ);

        // Try to land on the top of the left-most collider that overlaps our Z plane.
        float bestDist = std::numeric_limits<float>::max();
        const float skin = 0.05f;
        if (colMins && colMaxs) {
            for (size_t i = 0; i < colMins->size(); ++i) {
                const auto& cMin = (*colMins)[i];
                const auto& cMax = (*colMaxs)[i];
                // Require overlap on Z for our locked plane.
                if (levelMidZ + player.halfExtents.z < cMin.z || levelMidZ - player.halfExtents.z > cMax.z) {
                    continue;
                }
                // X extent must fit the player.
                float minX = cMin.x + player.halfExtents.x + skin;
                float maxX = cMax.x - player.halfExtents.x - skin;
                if (minX > maxX) {
                    continue;
                }
                float clampedX = std::clamp(desiredX, minX, maxX);
                float dist = std::abs(clampedX - desiredX);
                if (dist < bestDist) {
                    bestDist = dist;
                    spawnPos.x = clampedX;
                    spawnPos.y = cMax.y + player.halfExtents.y + skin;
                }
            }
        }

        player.pos = spawnPos;
    };

    auto tryLoadFromRoot = [&](Model& outModel, const std::filesystem::path& root) {
        bool loaded = false;

        // Preferred names
        const auto gltfPreferred = root / "scene.gltf";
        const auto glbPreferred = root / "scene.glb";
        if (std::filesystem::exists(gltfPreferred)) {
            loaded = outModel.LoadFromGlb(gltfPreferred, &modelError);  // supports .gltf/.glb
        }
        if (!loaded && std::filesystem::exists(glbPreferred)) {
            loaded = outModel.LoadFromGlb(glbPreferred, &modelError);
        }

        // Fallback: load the first glTF/GLB we can find in this root.
        if (!loaded) {
            std::vector<std::filesystem::path> candidates;
            for (const auto& entry : std::filesystem::directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".gltf" || ext == ".glb") {
                    candidates.push_back(entry.path());
                }
            }
            std::sort(candidates.begin(), candidates.end());
            for (const auto& c : candidates) {
                loaded = outModel.LoadFromGlb(c, &modelError);
                if (loaded) break;
            }
        }

        return loaded;
    };

    auto loadLevel = [&](int levelIndex) {
        if (levelIndex < 0 || levelIndex >= static_cast<int>(levelRoots.size())) {
            return false;
        }
        Model newModel;
        modelError.clear();

        const auto& root = levelRoots[levelIndex];
        bool loaded = tryLoadFromRoot(newModel, root);

        // Legacy fallbacks to keep previous behavior if new layout is missing.
        if (!loaded) {
            if (std::filesystem::exists(fbxPath)) {
                loaded = newModel.LoadFromFbx(fbxPath, &modelError);
            }
        }
        if (!loaded) {
            if (std::filesystem::exists(objPath)) {
                loaded = newModel.LoadFromObj(objPath, &modelError);
            }
        }
        if (!loaded) {
            std::cerr << "Failed to load level index " << levelIndex << " from root "
                      << root.string() << ": " << modelError << std::endl;
            return false;
        }

        sceneModel.Destroy();
        sceneModel = std::move(newModel);
        currentLevel = levelIndex;
        updateDerivedBounds();
        applyColliderRefs();
        spawnPlayerAtLevelStart();
        size_t colliderCount = colMins ? colMins->size() : 0;
        std::cout << "Loaded level " << levelIndex << " (" << levelRoots[levelIndex].string()
                  << ") with " << colliderCount << " colliders.\n";

        // Spawn a couple of placeholder enemies with patrol ranges.
        enemies.clear();
        auto addEnemy = [&](float startX, float endX) {
            Enemy e;
            e.rangeMin = std::min(startX, endX);
            e.rangeMax = std::max(startX, endX);
            e.pos = glm::vec3(0.5f * (e.rangeMin + e.rangeMax), boundsMax.y + 2.0f, levelMidZ);
            float groundY = findGroundBelow(e.pos.x, e.pos.z, e.pos.y);
            e.pos.y = groundY + e.halfExtents.y + 0.05f;
            e.dir = 1.0f;
            enemies.push_back(e);
        };
        float span = extent.x;
        float leftStart = boundsMin.x + std::max(5.0f, span * 0.05f);
        float leftEnd = leftStart + std::max(8.0f, span * 0.15f);
        float midStart = boundsMin.x + span * 0.45f;
        float midEnd = midStart + std::max(8.0f, span * 0.12f);
        addEnemy(leftStart, leftEnd);
        addEnemy(midStart, midEnd);
        return true;
    };

    if (!loadLevel(0)) {
        std::cerr << "Unable to load initial level." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    glm::vec3 lightColor(1.0f, 0.96f, 0.86f);
    // Moderate ambient lighting
    glm::vec3 ambientColor(0.15f, 0.15f, 0.18f);

    float previousTime = static_cast<float>(glfwGetTime());

    // Skin width for collision resolution
    float SKIN = 0.02f;
    // Maximum penetration allowed before resolving
    float MAX_PENETRATION = 0.1f;

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
        float moveInputX = 0.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveInputX -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveInputX += 1.0f;
        if (moveInputX != 0.0f) {
            moveInputX = (moveInputX > 0.0f) ? 1.0f : -1.0f;
        }
        bool jumpPressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        player.Update(deltaTime,
                      moveInputX,
                      jumpPressed,
                      moveSpeed,
                      jumpSpeed,
                      gravity,
                      levelMidZ,
                      colMins,
                      colMaxs,
                      boundsMin.y,
                      SKIN,
                      MAX_PENETRATION);
        glm::vec3 pMin = player.pos - player.halfExtents;
        glm::vec3 pMax = player.pos + player.halfExtents;

        // Update enemies (simple patrol along X, locked to level Z, stick to ground or fall if no support).
        for (auto& e : enemies) {
            e.Update(deltaTime, gravity, levelMidZ, SKIN, colMins, colMaxs, findGroundBelow);
        }

        // Player/enemy interaction: stomp to kill, side contact kills player.
        bool playerDied = false;
        std::vector<size_t> enemiesToRemove;
        for (size_t i = 0; i < enemies.size(); ++i) {
            const auto& e = enemies[i];
            glm::vec3 eMin = e.pos - e.halfExtents;
            glm::vec3 eMax = e.pos + e.halfExtents;

            bool overlapXZ =
                pMax.x > eMin.x && pMin.x < eMax.x &&
                pMax.z > eMin.z && pMin.z < eMax.z;
            if (!overlapXZ) continue;

            // Stomp if moving downward and feet are near the enemy top.
            bool stomp =
                player.vel.y <= 0.0f &&
                pMin.y <= eMax.y + 0.05f &&
                (pMin.y - eMax.y) > -0.35f; // allow small penetration tolerance

            if (stomp) {
                enemiesToRemove.push_back(i);
                player.vel.y = jumpSpeed * 0.7f;
                player.grounded = false;
                player.pos.y = eMax.y + player.halfExtents.y + 0.05f;
            } else {
                // If the player's feet are clearly above the enemy top, ignore (no side-hit).
                if (pMin.y > eMax.y + 0.1f) {
                    continue;
                }
                playerDied = true;
                break;
            }
        }

        if (playerDied) {
            spawnPlayerAtLevelStart();
            continue;
        }
        if (!enemiesToRemove.empty()) {
            // Erase from back to front to avoid index invalidation.
            std::sort(enemiesToRemove.begin(), enemiesToRemove.end(), std::greater<size_t>());
            for (size_t idx : enemiesToRemove) {
                if (idx < enemies.size()) {
                    enemies.erase(enemies.begin() + static_cast<std::ptrdiff_t>(idx));
                }
            }
        }

        // Level transition: spherical trigger near the end of the level.
        float distToFlag = glm::length(player.pos - flagTriggerCenter);
        if (distToFlag <= flagTriggerRadius) {
            int nextLevel = currentLevel + 1;
            if (nextLevel < static_cast<int>(levelRoots.size())) {
                std::cout << "Reached flag. Loading level " << nextLevel
                          << " (trigger center " << flagTriggerCenter.x << "," << flagTriggerCenter.y << "," << flagTriggerCenter.z
                          << " radius " << flagTriggerRadius
                          << " player " << player.pos.x << "," << player.pos.y << "," << player.pos.z
                          << " dist " << distToFlag << ")...\n";
                if (loadLevel(nextLevel)) {
                    // Start next frame with new level state.
                    glfwPollEvents();
                    continue;
                }
            } else {
                std::cout << "Reached flag. No further levels configured.\n";
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

        // Draw enemies
        for (const auto& e : enemies) {
            glm::mat4 enemyModel = glm::translate(glm::mat4(1.0f), e.pos) * glm::scale(glm::mat4(1.0f), e.halfExtents);
            glm::mat3 enemyNormal = glm::mat3(glm::transpose(glm::inverse(enemyModel)));
            shaderProgram.SetMat4("uModel", enemyModel);
            shaderProgram.SetMat3("uNormalMatrix", enemyNormal);
            shaderProgram.SetVec4("uMaterial.baseColorFactor", glm::vec4(0.8f, 0.2f, 0.2f, 1.0f));
            shaderProgram.SetFloat("uMaterial.metallic", 0.0f);
            shaderProgram.SetFloat("uMaterial.roughness", 0.9f);
            shaderProgram.SetInt("uMaterial.hasBaseColorTex", 0);
            shaderProgram.SetInt("uMaterial.hasMetalRoughTex", 0);
            glBindVertexArray(playerVAO);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    sceneModel.Destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}


