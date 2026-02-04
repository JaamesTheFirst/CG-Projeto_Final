#include "UImanager.hpp"
#include <iostream>
#include <fstream>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

// ============================================================================
// Constructor / Destructor
// ============================================================================

UIManager::UIManager()
    : state_(GameState::MAIN_MENU)
    , prevState_(GameState::MAIN_MENU)
    , levelCount_(0)
    , selectedLevel_(-1)
    , completedLevel_(-1)
    , isFinalLevel_(false)
    , stateTime_(0.0f)
    , animProgress_(0.0f)
    , windowWidth_(1280)
    , windowHeight_(720)
    , mouseWasDown_(false)
    , rectVAO_(0), rectVBO_(0)
    , textVAO_(0), textVBO_(0)
    , rectShader_(0), textShader_(0)
    , fontTexture_(0)
    , fontHeight_(48.0f)
    , initialized_(false)
{
    for (int i = 0; i < 128; ++i) {
        chars_[i] = {};
    }
}

UIManager::~UIManager() {
    if (rectVAO_) glDeleteVertexArrays(1, &rectVAO_);
    if (rectVBO_) glDeleteBuffers(1, &rectVBO_);
    if (textVAO_) glDeleteVertexArrays(1, &textVAO_);
    if (textVBO_) glDeleteBuffers(1, &textVBO_);
    if (rectShader_) glDeleteProgram(rectShader_);
    if (textShader_) glDeleteProgram(textShader_);
    if (fontTexture_) glDeleteTextures(1, &fontTexture_);
}

// ============================================================================
// Initialization
// ============================================================================

static GLuint CompileShader(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "Shader compile error (" << name << "): " << log << std::endl;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint LinkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        std::cerr << "Link error: " << log << std::endl;
        glDeleteProgram(prog);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

bool UIManager::LoadFont(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Cannot open font: " << path << std::endl;
        return false;
    }
    
    auto size = file.tellg();
    file.seekg(0);
    std::vector<unsigned char> buffer(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    file.close();
    
    const int atlasW = 512, atlasH = 512;
    std::vector<unsigned char> bitmap(atlasW * atlasH);
    
    stbtt_bakedchar baked[96];
    int res = stbtt_BakeFontBitmap(buffer.data(), 0, fontHeight_, 
                                    bitmap.data(), atlasW, atlasH, 32, 96, baked);
    if (res <= 0) {
        std::cerr << "Failed to bake font, result: " << res << std::endl;
        return false;
    }
    
    for (int i = 32; i < 128; ++i) {
        const auto& b = baked[i - 32];
        auto& c = chars_[i];
        c.u0 = b.x0 / float(atlasW);
        c.v0 = b.y0 / float(atlasH);
        c.u1 = b.x1 / float(atlasW);
        c.v1 = b.y1 / float(atlasH);
        c.xoff = b.xoff;
        c.yoff = b.yoff;
        c.xadvance = b.xadvance;
        c.width = float(b.x1 - b.x0);
        c.height = float(b.y1 - b.y0);
    }
    
    glGenTextures(1, &fontTexture_);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    return true;
}

bool UIManager::Initialize(GLFWwindow* /*window*/, int width, int height, const std::filesystem::path& fontPath) {
    windowWidth_ = width;
    windowHeight_ = height;
    
    // Rectangle shader (for buttons, backgrounds, and textured quads)
    const char* rectVS = R"(
        #version 410 core
        layout(location=0) in vec2 aPos;
        out vec2 vUV;
        uniform vec4 uRect;  // x, y, w, h in NDC
        uniform vec4 uUVRect; // u0, v0, u1, v1
        void main() {
            vec2 p = uRect.xy + aPos * uRect.zw;
            gl_Position = vec4(p, 0.0, 1.0);
            vUV = mix(uUVRect.xy, uUVRect.zw, aPos);
        }
    )";
    const char* rectFS = R"(
        #version 410 core
        in vec2 vUV;
        out vec4 FragColor;
        uniform vec4 uColor;
        uniform sampler2D uTex;
        uniform int uUseTexture;
        void main() {
            if (uUseTexture == 1) {
                float alpha = texture(uTex, vUV).r;
                FragColor = vec4(uColor.rgb, alpha * uColor.a);
            } else {
                FragColor = uColor;
            }
        }
    )";
    
    GLuint vs = CompileShader(GL_VERTEX_SHADER, rectVS, "rectVS");
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, rectFS, "rectFS");
    if (vs && fs) rectShader_ = LinkProgram(vs, fs);
    
    // Text shader - NOT USED, using rect shader for text instead
    const char* textVS = R"(
        #version 410 core
        layout(location=0) in vec4 aVertex;
        void main() {
            gl_Position = vec4(aVertex.xy, 0.0, 1.0);
        }
    )";
    const char* textFS = R"(
        #version 410 core
        out vec4 FragColor;
        uniform vec3 uColor;
        void main() {
            FragColor = vec4(uColor, 1.0);
        }
    )";
    
    vs = CompileShader(GL_VERTEX_SHADER, textVS, "textVS");
    fs = CompileShader(GL_FRAGMENT_SHADER, textFS, "textFS");
    if (vs && fs) {
        textShader_ = LinkProgram(vs, fs);
    }
    
    // Unit quad for rectangles (0,0 to 1,1)
    float quad[] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
    glGenVertexArrays(1, &rectVAO_);
    glGenBuffers(1, &rectVBO_);
    glBindVertexArray(rectVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, rectVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    
    // Dynamic text VBO (not used currently, using rect shader)
    glGenVertexArrays(1, &textVAO_);
    glGenBuffers(1, &textVBO_);
    glBindVertexArray(textVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, textVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4 * 256, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    
    glBindVertexArray(0);
    
    if (!LoadFont(fontPath)) {
        std::cerr << "Warning: Font failed to load, text won't render" << std::endl;
    }
    
    SetupMainMenu();
    initialized_ = true;
    return true;
}

// ============================================================================
// State Management
// ============================================================================

void UIManager::SetState(GameState state) {
    if (state == state_) return;
    prevState_ = state_;
    state_ = state;
    stateTime_ = 0.0f;
    animProgress_ = 0.0f;
    
    switch (state) {
        case GameState::MAIN_MENU:
            SetupMainMenu();
            break;
        case GameState::LEVEL_SELECT:
            SetupLevelSelect();
            break;
        case GameState::PAUSED:
            SetupPauseMenu();
            break;
        case GameState::LEVEL_COMPLETE:
        case GameState::PLAYING:
            buttons_.clear();
            break;
    }
}

void UIManager::SetLevelCount(int count) {
    levelCount_ = count;
    if (state_ == GameState::LEVEL_SELECT) {
        SetupLevelSelect();
    }
}

void UIManager::ShowLevelComplete(int levelNum, bool isFinal) {
    completedLevel_ = levelNum;
    isFinalLevel_ = isFinal;
    SetState(GameState::LEVEL_COMPLETE);
}

void UIManager::OnResize(int w, int h) {
    windowWidth_ = w;
    windowHeight_ = h;
}

// ============================================================================
// Screen Setup
// ============================================================================

void UIManager::SetupMainMenu() {
    buttons_.clear();
    
    UIButton startBtn;
    startBtn.pos = {0.0f, -0.15f};
    startBtn.size = {0.35f, 0.12f};
    startBtn.text = "START GAME";
    startBtn.id = -1;
    startBtn.hovered = false;
    startBtn.visible = true;
    buttons_.push_back(startBtn);
    
    UIButton quitBtn;
    quitBtn.pos = {0.0f, -0.35f};
    quitBtn.size = {0.25f, 0.1f};
    quitBtn.text = "QUIT";
    quitBtn.id = -2;
    quitBtn.hovered = false;
    quitBtn.visible = true;
    buttons_.push_back(quitBtn);
}

void UIManager::SetupLevelSelect() {
    buttons_.clear();
    
    // Back button
    UIButton back;
    back.pos = {-0.75f, 0.8f};
    back.size = {0.18f, 0.1f};
    back.text = "BACK";
    back.id = -3;
    back.hovered = false;
    back.visible = true;
    buttons_.push_back(back);
    
    // Level buttons in a grid
    int cols = 3;
    float btnW = 0.22f;
    float btnH = 0.12f;
    float gapX = 0.28f;
    float gapY = 0.18f;
    float startY = 0.25f;
    
    for (int i = 0; i < levelCount_ && i < 9; ++i) {
        int row = i / cols;
        int col = i % cols;
        
        float x = (col - 1) * gapX;
        float y = startY - row * gapY;
        
        UIButton lvl;
        lvl.pos = {x, y};
        lvl.size = {btnW, btnH};
        lvl.text = "LEVEL " + std::to_string(i + 1);
        lvl.id = i;
        lvl.hovered = false;
        lvl.visible = true;
        buttons_.push_back(lvl);
    }
}

void UIManager::SetupPauseMenu() {
    buttons_.clear();
    
    UIButton resume;
    resume.pos = {0.0f, 0.1f};
    resume.size = {0.3f, 0.12f};
    resume.text = "RESUME";
    resume.id = -4;
    resume.hovered = false;
    resume.visible = true;
    buttons_.push_back(resume);
    
    UIButton menu;
    menu.pos = {0.0f, -0.1f};
    menu.size = {0.3f, 0.12f};
    menu.text = "MAIN MENU";
    menu.id = -5;
    menu.hovered = false;
    menu.visible = true;
    buttons_.push_back(menu);
}

// ============================================================================
// Update
// ============================================================================

void UIManager::Update(GLFWwindow* window, float dt) {
    stateTime_ += dt;
    
    // Animation progress (0 to 1 over 0.5 seconds)
    animProgress_ = std::min(1.0f, stateTime_ / 0.5f);
    
    // Skip input during PLAYING state
    if (state_ == GameState::PLAYING) {
        // Check for pause key (ESC)
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            SetState(GameState::PAUSED);
        }
        return;
    }
    
    // Handle level complete: wait for click or key to continue
    if (state_ == GameState::LEVEL_COMPLETE) {
        bool clicked = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool keyPressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS || 
                          glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
        
        if ((clicked || keyPressed) && stateTime_ > 1.0f) {
            if (isFinalLevel_) {
                SetState(GameState::MAIN_MENU);
            } else {
                // Load next level
                selectedLevel_ = completedLevel_ + 1;
                SetState(GameState::PLAYING);
            }
        }
        return;
    }
    
    // Get mouse position
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    float nx, ny;
    ScreenToNDC(mx, my, nx, ny);
    
    bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    bool clicked = mouseDown && !mouseWasDown_;
    mouseWasDown_ = mouseDown;
    
    // Update button hover states and handle clicks
    for (auto& btn : buttons_) {
        if (!btn.visible) continue;
        btn.hovered = IsPointInButton(nx, ny, btn);
        
        if (btn.hovered && clicked) {
            // Handle button action
            if (btn.id >= 0) {
                // Level button
                selectedLevel_ = btn.id;
                SetState(GameState::PLAYING);
            } else if (btn.id == -1) {
                // Start game -> level select
                SetState(GameState::LEVEL_SELECT);
            } else if (btn.id == -2) {
                // Quit
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            } else if (btn.id == -3) {
                // Back to main menu
                SetState(GameState::MAIN_MENU);
            } else if (btn.id == -4) {
                // Resume
                SetState(GameState::PLAYING);
            } else if (btn.id == -5) {
                // Main menu from pause
                SetState(GameState::MAIN_MENU);
            }
        }
    }
}

// ============================================================================
// Drawing
// ============================================================================

void UIManager::Draw() {
    if (state_ == GameState::PLAYING) return;
    if (!initialized_) return;
    
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    float alpha = animProgress_;
    
    if (state_ == GameState::MAIN_MENU) {
        // Dark overlay
        DrawRect(-1, -1, 2, 2, {0.02f, 0.02f, 0.05f, 1.0f});
        
        // Title with slight animation
        float bounce = std::sin(stateTime_ * 2.0f) * 0.02f;
        DrawTextCentered("SUPER MARIO 3D", 0.5f + bounce, 1.4f, {1.0f, 0.85f, 0.2f});
        
        // Buttons
        for (const auto& btn : buttons_) {
            DrawButton(btn);
        }
        
        // Footer
        DrawTextCentered("Use WASD to move, SPACE to jump", -0.7f, 0.35f, {0.4f, 0.4f, 0.5f});
        
    } else if (state_ == GameState::LEVEL_SELECT) {
        // Dark background
        DrawRect(-1, -1, 2, 2, {0.02f, 0.02f, 0.05f, 1.0f});
        
        // Title
        DrawTextCentered("SELECT LEVEL", 0.7f, 0.9f, {0.9f, 0.9f, 1.0f});
        
        // Buttons
        for (const auto& btn : buttons_) {
            DrawButton(btn);
        }
        
    } else if (state_ == GameState::LEVEL_COMPLETE) {
        // Semi-transparent overlay
        float overlayAlpha = std::min(0.85f, stateTime_ * 2.0f);
        DrawRect(-1, -1, 2, 2, {0.0f, 0.05f, 0.1f, overlayAlpha});
        
        // Victory text with scale animation
        float scale = 1.0f + std::max(0.0f, 0.5f - stateTime_) * 2.0f;
        
        if (isFinalLevel_) {
            DrawTextCentered("CONGRATULATIONS!", 0.3f, scale * 1.0f, {1.0f, 0.85f, 0.2f});
            DrawTextCentered("You completed all levels!", 0.05f, 0.5f, {0.8f, 0.9f, 0.8f});
        } else {
            DrawTextCentered("LEVEL COMPLETE!", 0.3f, scale * 1.0f, {0.3f, 1.0f, 0.4f});
            std::string lvlText = "Level " + std::to_string(completedLevel_ + 1) + " cleared!";
            DrawTextCentered(lvlText, 0.05f, 0.5f, {0.8f, 0.9f, 0.8f});
        }
        
        // Prompt to continue (fade in after delay)
        if (stateTime_ > 1.0f) {
            float promptAlpha = std::min(1.0f, (stateTime_ - 1.0f) * 2.0f);
            float blink = (std::sin(stateTime_ * 5.0f) + 1.0f) * 0.5f;
            std::string prompt = isFinalLevel_ ? "Click to return to menu" : "Click to continue";
            DrawTextCentered(prompt, -0.3f, 0.4f, {promptAlpha * blink, promptAlpha * blink, promptAlpha});
        }
        
    } else if (state_ == GameState::PAUSED) {
        // Dim overlay
        DrawRect(-1, -1, 2, 2, {0.0f, 0.0f, 0.0f, 0.7f});
        
        DrawTextCentered("PAUSED", 0.4f, 1.0f, {1.0f, 1.0f, 1.0f});
        
        for (const auto& btn : buttons_) {
            DrawButton(btn);
        }
    }
    
    glEnable(GL_DEPTH_TEST);
}

// ============================================================================
// Drawing Helpers
// ============================================================================

void UIManager::DrawRect(float x, float y, float w, float h, glm::vec4 color) {
    if (!rectShader_) return;
    
    glUseProgram(rectShader_);
    glUniform4f(glGetUniformLocation(rectShader_, "uRect"), x, y, w, h);
    glUniform4fv(glGetUniformLocation(rectShader_, "uColor"), 1, &color[0]);
    glUniform1i(glGetUniformLocation(rectShader_, "uUseTexture"), 0);
    
    glBindVertexArray(rectVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void UIManager::DrawButton(const UIButton& btn) {
    if (!btn.visible) return;
    
    // Button background
    glm::vec4 bgColor = btn.hovered 
        ? glm::vec4(0.3f, 0.5f, 0.7f, 0.9f)
        : glm::vec4(0.15f, 0.25f, 0.4f, 0.85f);
    
    float x = btn.pos.x - btn.size.x * 0.5f;
    float y = btn.pos.y - btn.size.y * 0.5f;
    DrawRect(x, y, btn.size.x, btn.size.y, bgColor);
    
    // Border
    float borderThick = 0.006f;
    glm::vec4 borderColor = btn.hovered 
        ? glm::vec4(0.5f, 0.8f, 1.0f, 1.0f)
        : glm::vec4(0.3f, 0.4f, 0.6f, 1.0f);
    DrawRect(x - borderThick, y - borderThick, btn.size.x + borderThick*2, borderThick, borderColor);
    DrawRect(x - borderThick, y + btn.size.y, btn.size.x + borderThick*2, borderThick, borderColor);
    DrawRect(x - borderThick, y, borderThick, btn.size.y, borderColor);
    DrawRect(x + btn.size.x, y, borderThick, btn.size.y, borderColor);
    
    // Text - centered in button
    // Convert button center from NDC to pixels
    float btnCenterPxX = (btn.pos.x + 1.0f) * 0.5f * float(windowWidth_);
    float btnCenterPxY = (1.0f - btn.pos.y) * 0.5f * float(windowHeight_);
    float btnPixelH = btn.size.y * 0.5f * float(windowHeight_);
    
    float textScale = btnPixelH / fontHeight_ * 0.5f;  // Text height is 50% of button height
    glm::vec3 textColor = btn.hovered ? glm::vec3(1.0f) : glm::vec3(0.9f);
    
    float textW = GetTextWidth(btn.text, textScale);
    float textH = fontHeight_ * textScale;
    
    // Center text on button center
    float pixelX = btnCenterPxX - textW * 0.5f;
    float pixelY = btnCenterPxY - textH * 0.1f;
    
    DrawText(btn.text, pixelX, pixelY, textScale, textColor);
}

float UIManager::GetTextWidth(const std::string& text, float scale) {
    float w = 0;
    for (unsigned char c : text) {
        if (c >= 32 && c < 128) {
            w += chars_[c].xadvance * scale;
        }
    }
    return w;
}

void UIManager::DrawText(const std::string& text, float x, float y, float scale, glm::vec3 color) {
    if (!rectShader_ || !fontTexture_) return;
    
    glUseProgram(rectShader_);
    glUniform1i(glGetUniformLocation(rectShader_, "uUseTexture"), 1);
    glUniform1i(glGetUniformLocation(rectShader_, "uTex"), 0);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    
    float cx = x;
    for (unsigned char c : text) {
        if (c < 32 || c >= 128) continue;
        
        const auto& ch = chars_[c];
        float px = cx + ch.xoff * scale;
        float py = y + ch.yoff * scale;
        float w = ch.width * scale;
        float h = ch.height * scale;
        
        // Convert pixel coords to NDC
        float ndcX = (px / float(windowWidth_)) * 2.0f - 1.0f;
        float ndcY = 1.0f - (py / float(windowHeight_)) * 2.0f;
        float ndcW = (w / float(windowWidth_)) * 2.0f;
        float ndcH = (h / float(windowHeight_)) * 2.0f;
        
        // Set uniforms for this character
        glUniform4f(glGetUniformLocation(rectShader_, "uRect"), ndcX, ndcY - ndcH, ndcW, ndcH);
        glUniform4f(glGetUniformLocation(rectShader_, "uColor"), color.r, color.g, color.b, 1.0f);
        // Flip only V coordinate (keep U normal)
        glUniform4f(glGetUniformLocation(rectShader_, "uUVRect"), ch.u0, ch.v1, ch.u1, ch.v0);
        
        glBindVertexArray(rectVAO_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        cx += ch.xadvance * scale;
    }
    
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUniform1i(glGetUniformLocation(rectShader_, "uUseTexture"), 0);
}

void UIManager::DrawTextCentered(const std::string& text, float ndcY, float scale, glm::vec3 color) {
    float w = GetTextWidth(text, scale);
    float px = (float(windowWidth_) - w) * 0.5f;
    float py = (1.0f - ndcY) * 0.5f * float(windowHeight_);
    DrawText(text, px, py, scale, color);
}

// ============================================================================
// Input Helpers
// ============================================================================

void UIManager::ScreenToNDC(double sx, double sy, float& nx, float& ny) {
    nx = float(sx) / float(windowWidth_) * 2.0f - 1.0f;
    ny = 1.0f - float(sy) / float(windowHeight_) * 2.0f;
}

bool UIManager::IsPointInButton(float x, float y, const UIButton& btn) {
    float hw = btn.size.x * 0.5f;
    float hh = btn.size.y * 0.5f;
    return x >= btn.pos.x - hw && x <= btn.pos.x + hw &&
           y >= btn.pos.y - hh && y <= btn.pos.y + hh;
}
