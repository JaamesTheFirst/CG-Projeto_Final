#include "Enemy.hpp"

#include <algorithm>
#include <cmath>

void Enemy::GetAABB(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = pos - halfExtents;
    outMax = pos + halfExtents;
}

void Enemy::Update(float dt,
                   float gravity,
                   float levelMidZ,
                   float skin,
                   const std::vector<glm::vec3>* colMins,
                   const std::vector<glm::vec3>* colMaxs,
                   const std::function<float(float, float, float)>& findGroundBelow) {
    vel = glm::vec3(dir * speed, 0.0f, 0.0f);

    // Sweep along X against level colliders; bounce on hit.
    float delta = vel.x * dt;
    float move = delta;
    glm::vec3 pMin = pos - halfExtents;
    glm::vec3 pMax = pos + halfExtents;
    float dirSign = (delta > 0.0f) ? 1.0f : -1.0f;
    float bestMove = move;

    if (colMins && colMaxs) {
        for (size_t i = 0; i < colMins->size(); ++i) {
            const auto& cMin = (*colMins)[i];
            const auto& cMax = (*colMaxs)[i];

            // Need overlap in Y/Z to block.
            if (pMax.y <= cMin.y || pMin.y >= cMax.y) continue;
            if (pMax.z <= cMin.z || pMin.z >= cMax.z) continue;

            float dist = (dirSign > 0.0f)
                ? cMin.x - pMax.x - skin
                : cMax.x - pMin.x + skin;

            if ((dirSign > 0.0f && dist >= 0.0f && dist < bestMove) ||
                (dirSign < 0.0f && dist <= 0.0f && dist > bestMove)) {
                bestMove = dist;
            }
        }
    }

    pos.x += bestMove;

    // Bounce if blocked.
    if (bestMove != move) {
        dir = -dir;
    }

    // Keep within patrol range; bounce at ends.
    if (pos.x > rangeMax) {
        pos.x = rangeMax;
        dir = -1.0f;
    } else if (pos.x < rangeMin) {
        pos.x = rangeMin;
        dir = 1.0f;
    }

    pos.z = levelMidZ;
    // Grounding: pick the highest surface below current height; never climb up to higher surfaces.
    float groundY = findGroundBelow ? findGroundBelow(pos.x, pos.z, pos.y + 0.1f) : 0.0f;
    float desiredY = groundY + halfExtents.y + 0.02f;
    if (desiredY < pos.y) {
        // Drop down toward the ground (fall if unsupported).
        pos.y = std::max(desiredY, pos.y - gravity * dt);
    } else {
        // Do not climb up onto taller platforms; stay at current height.
        pos.y = pos.y;
    }
}


