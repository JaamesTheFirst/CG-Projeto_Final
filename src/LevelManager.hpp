#pragma once

#include "Model.hpp"
#include "Player.hpp"
#include "Enemy.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <limits>

class ShaderProgram;

class LevelManager {
public:
    explicit LevelManager(const std::filesystem::path& assetsRoot);

    enum class DepthPolicy {
        LockToPlane,     // Classic 2D rail
        PlatformCenter,  // Middle of platform depth
        Free             // No Z restriction
    };

    bool LoadLevel(int levelIndex, Player& player, std::vector<Enemy>& enemies, std::string& modelError);
    bool LoadNext(Player& player, std::vector<Enemy>& enemies, std::string& modelError);

    int GetCurrentLevel() const { return currentLevel_; }
    int GetLevelCount() const { return static_cast<int>(levelRoots_.size()); }

    const glm::vec3& GetBoundsMin() const { return boundsMin_; }
    const glm::vec3& GetBoundsMax() const { return boundsMax_; }
    float GetDiagonal() const { return diag_; }
    float GetLevelMidZ() const { return levelMidZ_; }
    glm::vec3 GetFlagTriggerCenter() const { return flagTriggerCenter_; }
    float GetFlagTriggerRadius() const { return flagTriggerRadius_; }
    const std::vector<glm::vec3>* GetColliderMins() const { return colMins_; }
    const std::vector<glm::vec3>* GetColliderMaxs() const { return colMaxs_; }

    float FindGroundBelow(float x, float z, float maxY) const;

    void DrawScene(const ShaderProgram& shader) const;

private:
    void DiscoverLevels();
    bool TryLoadFromRoot(Model& outModel, const std::filesystem::path& root, std::string& modelError);
    void UpdateDerivedBounds();
    void ApplyColliderRefs();
    void SpawnPlayerAtLevelStart(Player& player);
    void AddDefaultEnemies(std::vector<Enemy>& enemies);
    float ResolveSpawnZOnPlatform(const glm::vec3& cMin, const glm::vec3& cMax, const Player& player) const;

    std::filesystem::path assetsRoot_;
    std::filesystem::path fbxPath_;
    std::filesystem::path objPath_;

    std::vector<std::filesystem::path> levelRoots_;
    Model sceneModel_;

    DepthPolicy depthPolicy_ = DepthPolicy::LockToPlane;
    float gameplayPlaneZ_ = 0.0f;
    bool hasPlaneOverride_ = false;

    glm::vec3 boundsMin_{0.0f};
    glm::vec3 boundsMax_{0.0f};
    glm::vec3 target_{0.0f};
    glm::vec3 extent_{0.0f};
    float diag_ = 50.0f;
    float levelMidZ_ = 0.0f;
    glm::vec3 flagTriggerCenter_{0.0f};
    float flagTriggerRadius_ = 10.0f;
    int currentLevel_ = 0;

    const std::vector<glm::vec3>* colMins_ = nullptr;
    const std::vector<glm::vec3>* colMaxs_ = nullptr;
};


