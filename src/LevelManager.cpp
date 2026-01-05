#include "LevelManager.hpp"
#include "ShaderProgram.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <limits>
#include <cmath>
#include <unordered_map>

LevelManager::LevelManager(const std::filesystem::path& assetsRoot)
    : assetsRoot_(assetsRoot),
      fbxPath_(assetsRoot / "models" / "tanabata-evening-kyoto-inspired-city-scene" / "source" / "testexport.fbx"),
      objPath_(assetsRoot / "models" / "map.obj") {
    DiscoverLevels();
}

void LevelManager::DiscoverLevels() {
    levelRoots_.clear();
    const auto levelsRoot = assetsRoot_ / "models" / "levels";
    if (std::filesystem::exists(levelsRoot)) {
        for (const auto& entry : std::filesystem::directory_iterator(levelsRoot)) {
            if (entry.is_directory()) {
                levelRoots_.push_back(entry.path());
            }
        }
    }

    // Prefer numeric ordering of folders named "lvlX" (X is integer).
    std::vector<std::pair<int, std::filesystem::path>> numbered;
    for (const auto& p : levelRoots_) {
        const auto name = p.filename().string();
        if (name.rfind("lvl", 0) == 0 && name.size() > 3) {
            try {
                int idx = std::stoi(name.substr(3));
                numbered.emplace_back(idx, p);
            } catch (...) {
                // ignore parse errors, will fall back to alpha
            }
        }
    }

    if (!numbered.empty()) {
        std::sort(numbered.begin(), numbered.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second.filename().string() < b.second.filename().string();
        });
        levelRoots_.clear();
        for (const auto& np : numbered) {
            levelRoots_.push_back(np.second);
        }
    }

    if (levelRoots_.empty()) {
        std::cerr << "No levels found under " << levelsRoot << ". Expected folders named lvlX.\n";
    }
}

bool LevelManager::TryLoadFromRoot(Model& outModel, const std::filesystem::path& root, std::string& modelError) {
    bool loaded = false;

    const auto gltfPreferred = root / "scene.gltf";
    const auto glbPreferred = root / "scene.glb";
    if (std::filesystem::exists(gltfPreferred)) {
        loaded = outModel.LoadFromGlb(gltfPreferred, &modelError);
    }
    if (!loaded && std::filesystem::exists(glbPreferred)) {
        loaded = outModel.LoadFromGlb(glbPreferred, &modelError);
    }

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
}

void LevelManager::UpdateDerivedBounds() {
    boundsMin_ = sceneModel_.GetBoundsMin();
    boundsMax_ = sceneModel_.GetBoundsMax();
    target_ = 0.5f * (boundsMin_ + boundsMax_);
    extent_ = boundsMax_ - boundsMin_;
    diag_ = glm::length(extent_);
    if (diag_ <= 0.001f) {
        diag_ = 50.0f;
    }
    if (!hasPlaneOverride_) {
        gameplayPlaneZ_ = boundsMin_.z + extent_.z * 0.10f;
    }
    levelMidZ_ = gameplayPlaneZ_;
    float triggerDepth = std::max(5.0f, extent_.x * 0.05f);
    flagTriggerCenter_ = glm::vec3(boundsMax_.x - triggerDepth * 0.5f,
                                   0.5f * (boundsMin_.y + boundsMax_.y),
                                   levelMidZ_);
    flagTriggerRadius_ = std::max({10.0f, triggerDepth * 1.5f, extent_.z * 0.5f});
}

void LevelManager::ApplyColliderRefs() {
    colMins_ = &sceneModel_.GetColliderMins();
    colMaxs_ = &sceneModel_.GetColliderMaxs();
}

float LevelManager::FindGroundBelow(float x, float z, float maxY) const {
    float floorY = boundsMin_.y;
    if (colMins_ && colMaxs_) {
        for (size_t i = 0; i < colMins_->size(); ++i) {
            const auto& cMin = (*colMins_)[i];
            const auto& cMax = (*colMaxs_)[i];
            if (x < cMin.x || x > cMax.x) continue;
            if (z < cMin.z || z > cMax.z) continue;
            if (cMax.y <= maxY + 0.001f) {
                floorY = std::max(floorY, cMax.y);
            }
        }
    }
    return floorY;
}

float LevelManager::ResolveSpawnZOnPlatform(const glm::vec3& cMin,
                                            const glm::vec3& cMax,
                                            const Player& player) const {
    const float skin = 0.05f;
    float minZ = cMin.z + player.halfExtents.z + skin;
    float maxZ = cMax.z - player.halfExtents.z - skin;

    switch (depthPolicy_) {
        case DepthPolicy::LockToPlane:
            return std::clamp(gameplayPlaneZ_, minZ, maxZ);
        case DepthPolicy::PlatformCenter:
            return 0.5f * (minZ + maxZ);
        case DepthPolicy::Free:
            return gameplayPlaneZ_; // no clamping
        default:
            return gameplayPlaneZ_;
    }
}

void LevelManager::SpawnPlayerAtLevelStart(Player& player) {
    player.vel = glm::vec3(0.0f);
    player.grounded = false;
    const float skin = 0.05f;

    // IMPORTANT: our colliders are per-triangle AABBs, so "picking a platform collider" is unreliable.
    // Instead, we sample a small X/Z grid and build a histogram of ground heights.
    // The most common height is usually the main floor/path; we spawn on that and lock the Z rail there.

    float fallbackX = boundsMin_.x + player.halfExtents.x + 1.0f;
    float fallbackY = boundsMax_.y + player.halfExtents.y + 5.0f;
    glm::vec3 spawnPos(fallbackX, fallbackY, gameplayPlaneZ_);

    const float padX = player.halfExtents.x + skin;
    const float padZ = player.halfExtents.z + skin;
    const float xMin = boundsMin_.x + padX;
    const float xMax = boundsMax_.x - padX;
    const float zMin = boundsMin_.z + padZ;
    const float zMax = boundsMax_.z - padZ;

    const float maxYQuery = boundsMax_.y + 1000.0f;
    const float groundEps = 0.01f;
    const float binSize = 0.25f; // quantize ground heights

    struct Sample { float x, z, y; };
    std::vector<Sample> samples;
    samples.reserve(12 * 40);

    auto collectSamples = [&](float xStartFrac, float xEndFrac, int xSamples, int zSamples) {
        float width = (xMax - xMin);
        float xStart = xMin + width * std::clamp(xStartFrac, 0.0f, 1.0f);
        float xEnd = xMin + width * std::clamp(xEndFrac, 0.0f, 1.0f);
        for (int xi = 0; xi < xSamples; ++xi) {
            float tX = (xSamples == 1) ? 0.0f : static_cast<float>(xi) / static_cast<float>(xSamples - 1);
            float x = xStart + tX * (xEnd - xStart);
            x = std::clamp(x, xMin, xMax);
            for (int zi = 0; zi < zSamples; ++zi) {
                float tZ = (zSamples == 1) ? 0.0f : static_cast<float>(zi) / static_cast<float>(zSamples - 1);
                float z = zMin + tZ * (zMax - zMin);
                float gY = FindGroundBelow(x, z, maxYQuery);
                if (gY <= boundsMin_.y + groundEps) continue;
                samples.push_back({x, z, gY});
            }
        }
    };

    // Pass 1: try the left portion of the map (start area might not begin at exact boundsMin.x).
    collectSamples(0.0f, 0.40f, 12, 40);
    // Pass 2: if still nothing, broaden to whole level to find any valid floor.
    if (samples.empty()) {
        collectSamples(0.0f, 1.0f, 18, 50);
    }

    // Histogram by quantized Y.
    int bestBin = 0;
    int bestCount = 0;
    float bestBinY = 0.0f;
    if (!samples.empty()) {
        std::unordered_map<int, int> counts;
        counts.reserve(samples.size());
        for (const auto& s : samples) {
            int bin = static_cast<int>(std::floor(s.y / binSize));
            counts[bin] += 1;
        }
        for (const auto& [bin, count] : counts) {
            float binY = bin * binSize;
            if (count > bestCount || (count == bestCount && binY < bestBinY)) {
                bestCount = count;
                bestBin = bin;
                bestBinY = binY;
            }
        }

        // Pick a sample in the dominant bin:
        // - Choose the leftmost X (start side)
        // - For Z, choose the center of the main path at that X (average Z over nearby samples)
        bool found = false;
        float chosenX = spawnPos.x;
        float chosenZ = spawnPos.z;

        // First pass: find leftmost X in this bin.
        for (const auto& s : samples) {
            int bin = static_cast<int>(std::floor(s.y / binSize));
            if (bin != bestBin) continue;
            if (!found || s.x < chosenX) {
                chosenX = s.x;
                found = true;
            }
        }

        // Second pass: average Z of samples near that leftmost X (gives "middle of the corridor" depth).
        if (found) {
            const float xBand = 0.75f; // consider samples within this X distance of the leftmost X
            float zSum = 0.0f;
            int zCount = 0;
            float zMinSeen = std::numeric_limits<float>::infinity();
            float zMaxSeen = -std::numeric_limits<float>::infinity();

            for (const auto& s : samples) {
                int bin = static_cast<int>(std::floor(s.y / binSize));
                if (bin != bestBin) continue;
                if (std::abs(s.x - chosenX) > xBand) continue;
                zSum += s.z;
                zCount += 1;
                zMinSeen = std::min(zMinSeen, s.z);
                zMaxSeen = std::max(zMaxSeen, s.z);
            }

            if (zCount > 0) {
                float zAvg = zSum / static_cast<float>(zCount);
                // Clamp within observed range for safety.
                chosenZ = std::clamp(zAvg, zMinSeen, zMaxSeen);
            } else {
                // Fallback: pick the most "viewer side" Z (previous behavior).
                float bestZ = std::numeric_limits<float>::infinity();
                for (const auto& s : samples) {
                    int bin = static_cast<int>(std::floor(s.y / binSize));
                    if (bin != bestBin) continue;
                    if (std::abs(s.x - chosenX) > xBand) continue;
                    if (s.z < bestZ) bestZ = s.z;
                }
                if (bestZ != std::numeric_limits<float>::infinity()) {
                    chosenZ = bestZ;
                }
            }
        }

        if (found) {
            // Validate once more at the chosen point.
            float verifyY = FindGroundBelow(chosenX, chosenZ, maxYQuery);
            if (verifyY > boundsMin_.y + groundEps) {
                gameplayPlaneZ_ = chosenZ;
                levelMidZ_ = chosenZ;
                hasPlaneOverride_ = true;
                spawnPos.x = chosenX;
                spawnPos.z = chosenZ;
                spawnPos.y = verifyY + player.halfExtents.y + skin + 0.25f;
            }
        }
    }

    std::cout << "[SpawnDebug] level " << currentLevel_
              << " samples=" << samples.size()
              << " dominantBinY=" << bestBinY
              << " dominantCount=" << bestCount
              << " final spawn (" << spawnPos.x << "," << spawnPos.y << "," << spawnPos.z << ")"
              << " gameplayPlaneZ=" << gameplayPlaneZ_
              << " hasPlaneOverride=" << (hasPlaneOverride_ ? 1 : 0)
              << std::endl;

    player.pos = spawnPos;
}

void LevelManager::AddDefaultEnemies(std::vector<Enemy>& enemies) {
    enemies.clear();
    auto addEnemy = [&](float startX, float endX) {
        Enemy e;
        e.rangeMin = std::min(startX, endX);
        e.rangeMax = std::max(startX, endX);
        e.pos = glm::vec3(0.5f * (e.rangeMin + e.rangeMax), boundsMax_.y + 2.0f, levelMidZ_);
        float groundY = FindGroundBelow(e.pos.x, e.pos.z, e.pos.y);
        e.pos.y = groundY + e.halfExtents.y + 0.05f;
        e.dir = 1.0f;
        enemies.push_back(e);
    };
    float span = extent_.x;
    float leftStart = boundsMin_.x + std::max(5.0f, span * 0.05f);
    float leftEnd = leftStart + std::max(8.0f, span * 0.15f);
    float midStart = boundsMin_.x + span * 0.45f;
    float midEnd = midStart + std::max(8.0f, span * 0.12f);
    addEnemy(leftStart, leftEnd);
    addEnemy(midStart, midEnd);
}

bool LevelManager::LoadLevel(int levelIndex, Player& player, std::vector<Enemy>& enemies, std::string& modelError) {
    if (levelIndex < 0 || levelIndex >= static_cast<int>(levelRoots_.size())) {
        return false;
    }
    depthPolicy_ = DepthPolicy::LockToPlane;
    hasPlaneOverride_ = false;

    Model newModel;
    modelError.clear();

    const auto& root = levelRoots_[levelIndex];
    bool loaded = TryLoadFromRoot(newModel, root, modelError);

    if (!loaded && std::filesystem::exists(fbxPath_)) {
        loaded = newModel.LoadFromFbx(fbxPath_, &modelError);
    }
    if (!loaded && std::filesystem::exists(objPath_)) {
        loaded = newModel.LoadFromObj(objPath_, &modelError);
    }
    if (!loaded) {
        std::cerr << "Failed to load level index " << levelIndex << " from root "
                  << root.string() << ": " << modelError << std::endl;
        return false;
    }

    sceneModel_.Destroy();
    sceneModel_ = std::move(newModel);
    currentLevel_ = levelIndex;
    UpdateDerivedBounds();
    ApplyColliderRefs();
    SpawnPlayerAtLevelStart(player);
    AddDefaultEnemies(enemies);
    size_t colliderCount = colMins_ ? colMins_->size() : 0;
    std::cout << "Loaded level " << levelIndex << " (" << levelRoots_[levelIndex].string()
              << ") with " << colliderCount << " colliders. "
              << "Spawn pos: " << player.pos.x << "," << player.pos.y << "," << player.pos.z
              << " levelMidZ=" << levelMidZ_
              << " boundsMinY=" << boundsMin_.y << " boundsMaxY=" << boundsMax_.y << "\n";
    return true;
}

bool LevelManager::LoadNext(Player& player, std::vector<Enemy>& enemies, std::string& modelError) {
    int nextLevel = currentLevel_ + 1;
    if (nextLevel >= static_cast<int>(levelRoots_.size())) {
        return false;
    }
    return LoadLevel(nextLevel, player, enemies, modelError);
}

void LevelManager::DrawScene(const ShaderProgram& shader) const {
    sceneModel_.Draw(shader);
}



