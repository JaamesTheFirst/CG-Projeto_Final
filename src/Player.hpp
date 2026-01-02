#pragma once

#include <glm/glm.hpp>
#include <vector>

struct Player {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    glm::vec3 halfExtents{0.5f, 1.0f, 0.5f};
    bool grounded = false;

    void Update(float dt,
                float moveInputX,
                bool jumpPressed,
                float moveSpeed,
                float jumpSpeed,
                float gravity,
                float levelMidZ,
                const std::vector<glm::vec3>* colMins,
                const std::vector<glm::vec3>* colMaxs,
                float boundsMinY,
                float skin,
                float maxPenetration);

    void GetAABB(glm::vec3& outMin, glm::vec3& outMax) const;
};


