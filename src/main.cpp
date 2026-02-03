#include "Model.hpp"
#include "ShaderProgram.hpp"
#include "Player.hpp"
#include "Enemy.hpp"
#include "LevelManager.hpp"

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
    // VSync adds noticeable input latency, especially in FPS-style camera mode.
    // We default to off; you can re-enable it later if you prefer smoother frame pacing.
    glfwSwapInterval(0);

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

    Player player;
    std::vector<Enemy> enemies;
    LevelManager levelManager(assetsRoot);

    std::string modelError;
    int startLevel = 0;
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg.rfind("lvl", 0) == 0 && arg.size() > 3) {
            try {
                startLevel = std::max(0, std::stoi(arg.substr(3)) - 1); // lvl1 -> index 0
            } catch (...) {
                startLevel = 0;
            }
        }
    }

    if (!levelManager.LoadLevel(startLevel, player, enemies, modelError)) {
        std::cerr << "Unable to load initial level." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    camera.distance = levelManager.GetDiagonal() * 1.5f;
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

    // Camera / movement mode toggle (2D <-> 3D)
    bool want3D = false;
    float modeBlend = 0.0f; // 0 = 2D, 1 = 3D
    bool prevToggleKey = false;

    // 3D "shooter" camera state (yaw/pitch in radians).
    float camYaw = 0.0f;              // yaw=0 => looking +X
    float camPitch = glm::radians(-18.0f);
    bool mouseCaptured = false;
    bool firstMouse = true;
    bool skipNextMouseFrame = false;
    double lastMouseX = 0.0, lastMouseY = 0.0;

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

        // Toggle camera/movement mode.
        bool toggleKey = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (toggleKey && !prevToggleKey) {
            want3D = !want3D;
            // When returning to 2D, snap player back onto the current rail.
            if (!want3D) {
                player.pos.z = levelManager.GetLevelMidZ();
                player.vel.z = 0.0f;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                mouseCaptured = false;
            }
            if (want3D) {
                // Use HIDDEN instead of DISABLED - works better in WSL
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                mouseCaptured = true;
                firstMouse = true;
                // Center cursor on mode switch
                int winW, winH;
                glfwGetWindowSize(window, &winW, &winH);
                glfwSetCursorPos(window, winW / 2.0, winH / 2.0);
            }
        }
        prevToggleKey = toggleKey;

        // Camera transition ("party trick"):
        // Smoothly blend camera + projection between 2D and 3D, but keep controls immediate.
        {
            float target = want3D ? 1.0f : 0.0f;
            // Roughly ~0.15-0.25s feel depending on frame rate.
            const float blendSpeed = 10.0f;
            float k = 1.0f - std::exp(-blendSpeed * deltaTime);
            modeBlend = modeBlend + (target - modeBlend) * k;
            if (std::abs(target - modeBlend) < 1e-4f) {
                modeBlend = target;
            }
        }

        // Camera setup.
        // 2D: ortho side view. 3D: third-person shooter camera (mouse look).
        constexpr float kOrthoHalfHeight = 18.0f;
        glm::vec3 lookTarget2D = player.pos + glm::vec3(0.0f, 2.0f, 0.0f);
        glm::vec3 cameraPos2D = lookTarget2D + glm::vec3(0.0f, 0.0f, 60.0f);

        // Update mouse look in 3D mode
        // Using HIDDEN cursor + manual edge wrapping (works better in WSL than DISABLED)
        if (want3D && mouseCaptured) {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);

            int winW, winH;
            glfwGetWindowSize(window, &winW, &winH);
            double cx = winW / 2.0;
            double cy = winH / 2.0;

            if (firstMouse) {
                lastMouseX = cx;
                lastMouseY = cy;
                glfwSetCursorPos(window, cx, cy);
                firstMouse = false;
                skipNextMouseFrame = true;
            } else if (skipNextMouseFrame) {
                // Skip this frame's delta (we just initialized)
                lastMouseX = mx;
                lastMouseY = my;
                skipNextMouseFrame = false;
            } else {
                double dx = mx - lastMouseX;
                double dy = my - lastMouseY;

                // Clamp deltas to reasonable values (prevents WSL weirdness)
                const double maxDelta = 50.0;
                dx = std::clamp(dx, -maxDelta, maxDelta);
                dy = std::clamp(dy, -maxDelta, maxDelta);

                // Check if cursor is getting close to edges
                const double edgeMargin = 200.0;  // Larger margin = recenter earlier
                bool nearEdgeX = (mx < edgeMargin || mx > winW - edgeMargin);
                bool nearEdgeY = (my < edgeMargin || my > winH - edgeMargin);

                // Reduce rotation speed when near edge (prevents "burst" feeling)
                if (nearEdgeX) dx *= 0.5;
                if (nearEdgeY) dy *= 0.5;

                // Apply rotation
                const float sens = 0.003f;
                camYaw += static_cast<float>(dx) * sens;
                camPitch -= static_cast<float>(dy) * sens;
                camPitch = std::clamp(camPitch, glm::radians(-89.0f), glm::radians(89.0f));

                // Update last position
                lastMouseX = mx;
                lastMouseY = my;

                // Recenter the affected axis
                if (nearEdgeX || nearEdgeY) {
                    double newX = nearEdgeX ? cx : mx;
                    double newY = nearEdgeY ? cy : my;
                    glfwSetCursorPos(window, newX, newY);
                    if (nearEdgeX) lastMouseX = cx;
                    if (nearEdgeY) lastMouseY = cy;
                }
            }
        }

        // 3D camera (third-person over-the-shoulder): slightly above/behind the player,
        // still driven by the same mouse-look forward vector.
        glm::vec3 forward3D(
            std::cos(camPitch) * std::cos(camYaw),
            std::sin(camPitch),
            std::cos(camPitch) * std::sin(camYaw)
        );
        forward3D = glm::normalize(forward3D);
        glm::vec3 up(0.0f, 1.0f, 0.0f);
        glm::vec3 right3D = glm::normalize(glm::cross(forward3D, up));
        constexpr float kLookHeight = 2.0f;
        constexpr float kLookAhead = 6.0f;
        constexpr float kCamBack = 6.0f;
        constexpr float kCamUp = 3.0f;
        constexpr float kShoulder = 1.0f;
        glm::vec3 lookTarget3D = player.pos + glm::vec3(0.0f, kLookHeight, 0.0f) + forward3D * kLookAhead;
        glm::vec3 cameraPos3D = (player.pos + glm::vec3(0.0f, kLookHeight, 0.0f))
                                - forward3D * kCamBack
                                + glm::vec3(0.0f, kCamUp, 0.0f)
                                + right3D * kShoulder;

        float t = std::clamp(modeBlend, 0.0f, 1.0f);
        // Smoothstep so it eases in/out a bit (feels more like a "rotate into 3D" trick).
        float tSmooth = t * t * (3.0f - 2.0f * t);
        glm::vec3 lookTarget = glm::mix(lookTarget2D, lookTarget3D, tSmooth);
        glm::vec3 cameraPos = glm::mix(cameraPos2D, cameraPos3D, tSmooth);
        glm::mat4 view = glm::lookAt(cameraPos, lookTarget, glm::vec3(0.0f, 1.0f, 0.0f));

        float orthoHalfWidth = kOrthoHalfHeight * aspect;
        glm::mat4 orthoProj = glm::ortho(-orthoHalfWidth, orthoHalfWidth,
                                         -kOrthoHalfHeight, kOrthoHalfHeight,
                                         0.1f, 500.0f);
        glm::mat4 perspProj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 500.0f);
        // Matrix mix isn't available in all GLM versions; lerp manually.
        // Not physically "correct", but looks great for a stylized transition.
        glm::mat4 projection = orthoProj * (1.0f - tSmooth) + perspProj * tSmooth;

        glm::mat4 model = glm::mat4(1.0f);
        glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));

        // Player input & physics:
        // - 2D mode: A/D only, Z locked to the rail.
        // - 3D mode: third-person shooter controls (WASD relative to camera).
        float moveInputX = 0.0f;
        float moveInputZ = 0.0f;
        if (!want3D) {
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveInputX -= 1.0f;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveInputX += 1.0f;
        } else {
            // Build movement direction from camera basis on the ground plane.
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            glm::vec3 right3D = glm::normalize(glm::cross(forward3D, up)); // yaw=0 -> right is +Z
            glm::vec3 fwdFlat = glm::normalize(glm::vec3(forward3D.x, 0.0f, forward3D.z));
            glm::vec3 rightFlat = glm::normalize(glm::vec3(right3D.x, 0.0f, right3D.z));

            glm::vec3 moveDir(0.0f);
            if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += fwdFlat;
            if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= fwdFlat;
            if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += rightFlat;
            if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= rightFlat;
            if (glm::length(moveDir) > 0.001f) {
                moveDir = glm::normalize(moveDir);
                moveInputX = moveDir.x;
                moveInputZ = moveDir.z;
            }
        }
        bool jumpPressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        player.Update(deltaTime,
                      moveInputX,
                      moveInputZ,
                      jumpPressed,
                      moveSpeed,
                      jumpSpeed,
                      gravity,
                      !want3D,
                      levelManager.GetLevelMidZ(),
                      levelManager.GetColliderMins(),
                      levelManager.GetColliderMaxs(),
                      levelManager.GetBoundsMin().y,
                      SKIN,
                      MAX_PENETRATION);
        glm::vec3 pMin = player.pos - player.halfExtents;
        glm::vec3 pMax = player.pos + player.halfExtents;

        auto groundFn = [&](float x, float z, float maxY) { return levelManager.FindGroundBelow(x, z, maxY); };
        // Update enemies (simple patrol along X, locked to level Z, stick to ground or fall if no support).
        for (auto& e : enemies) {
            e.Update(deltaTime, gravity, levelManager.GetLevelMidZ(), SKIN, levelManager.GetColliderMins(), levelManager.GetColliderMaxs(), groundFn);
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
            // Reload current level to respawn player and reset enemies.
            levelManager.LoadLevel(levelManager.GetCurrentLevel(), player, enemies, modelError);
            camera.distance = levelManager.GetDiagonal() * 1.5f;
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
        float distToFlag = glm::length(player.pos - levelManager.GetFlagTriggerCenter());
        if (distToFlag <= levelManager.GetFlagTriggerRadius()) {
            int nextLevel = levelManager.GetCurrentLevel() + 1;
            if (nextLevel < levelManager.GetLevelCount()) {
                std::cout << "Reached flag. Loading level " << nextLevel
                          << " (trigger center " << levelManager.GetFlagTriggerCenter().x << "," << levelManager.GetFlagTriggerCenter().y << "," << levelManager.GetFlagTriggerCenter().z
                          << " radius " << levelManager.GetFlagTriggerRadius()
                          << " player " << player.pos.x << "," << player.pos.y << "," << player.pos.z
                          << " dist " << distToFlag << ")...\n";
                if (levelManager.LoadLevel(nextLevel, player, enemies, modelError)) {
                    camera.distance = levelManager.GetDiagonal() * 1.5f;
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

        levelManager.DrawScene(shaderProgram);

        // Draw player cube (in 2D and 3D third-person)
        if (true) {
            // The debug cube indices have mixed winding for some faces; with back-face culling on,
            // the cube can look "transparent" from certain angles. Disable culling just for it.
            glDisable(GL_CULL_FACE);
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
            glEnable(GL_CULL_FACE);
        }

        // Draw enemies
        for (const auto& e : enemies) {
            glDisable(GL_CULL_FACE);
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
            glEnable(GL_CULL_FACE);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}


