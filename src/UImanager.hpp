#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include <functional>

// Game UI states
enum class GameState {
    MAIN_MENU,       // Title screen with "Press Start"
    LEVEL_SELECT,    // Choose which level to play
    PLAYING,         // In-game
    LEVEL_COMPLETE,  // Victory animation after reaching flag
    PAUSED           // Pause menu during gameplay
};

// Simple button structure
struct UIButton {
    glm::vec2 pos;       // Center position in NDC (-1 to 1)
    glm::vec2 size;      // Size in NDC
    std::string text;
    int id;              // Button identifier (-1 = special action, >=0 = level index)
    bool hovered;
    bool visible;
};

class UIManager {
public:
    UIManager();
    ~UIManager();
    
    // Initialize with window and font
    bool Initialize(GLFWwindow* window, int width, int height, const std::filesystem::path& fontPath);
    
    // Called every frame
    void Update(GLFWwindow* window, float deltaTime);
    void Draw();
    
    // State management
    GameState GetState() const { return state_; }
    void SetState(GameState state);
    
    // Level selection
    int GetSelectedLevel() const { return selectedLevel_; }
    void ClearSelection() { selectedLevel_ = -1; }
    void SetLevelCount(int count);
    
    // Trigger level complete animation
    void ShowLevelComplete(int levelNum, bool isFinalLevel);
    
    // Window resize
    void OnResize(int width, int height);

private:
    // Setup different screens
    void SetupMainMenu();
    void SetupLevelSelect();
    void SetupPauseMenu();
    
    // Rendering helpers
    void DrawRect(float x, float y, float w, float h, glm::vec4 color);
    void DrawText(const std::string& text, float x, float y, float scale, glm::vec3 color);
    void DrawTextCentered(const std::string& text, float y, float scale, glm::vec3 color);
    void DrawButton(const UIButton& btn);
    float GetTextWidth(const std::string& text, float scale);
    
    // Input
    bool IsPointInButton(float x, float y, const UIButton& btn);
    void ScreenToNDC(double sx, double sy, float& nx, float& ny);
    
    // Font loading
    bool LoadFont(const std::filesystem::path& path);
    
    // Current state
    GameState state_;
    GameState prevState_;
    
    // Buttons for current screen
    std::vector<UIButton> buttons_;
    
    // Level info
    int levelCount_;
    int selectedLevel_;
    int completedLevel_;
    bool isFinalLevel_;
    
    // Animation
    float stateTime_;          // Time since state change
    float animProgress_;       // 0-1 animation progress
    
    // Window
    int windowWidth_;
    int windowHeight_;
    
    // Input state
    bool mouseWasDown_;
    
    // OpenGL resources
    GLuint rectVAO_, rectVBO_;
    GLuint textVAO_, textVBO_;
    GLuint rectShader_;
    GLuint textShader_;
    GLuint fontTexture_;
    
    // Font character data
    struct CharData {
        float u0, v0, u1, v1;
        float xoff, yoff, xadvance;
        float width, height;
    };
    CharData chars_[128];
    float fontHeight_;
    
    bool initialized_;
};
