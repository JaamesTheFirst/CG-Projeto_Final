#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <functional>

struct Enemy {
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    glm::vec3 halfExtents{0.6f, 0.6f, 0.6f};
    float speed = 6.0f;
    float dir = 1.0f;
    float rangeMin = 0.0f;
    float rangeMax = 0.0f;

    void Update(float dt,
                float gravity,
                float levelMidZ,
                float skin,
                const std::vector<glm::vec3>* colMins,
                const std::vector<glm::vec3>* colMaxs,
                const std::function<float(float, float, float)>& findGroundBelow);

    void GetAABB(glm::vec3& outMin, glm::vec3& outMax) const;
};


