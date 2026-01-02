#include "Player.hpp"

#include <algorithm>
#include <cmath>

void Player::GetAABB(glm::vec3& outMin, glm::vec3& outMax) const {
    outMin = pos - halfExtents;
    outMax = pos + halfExtents;
}

void Player::Update(float dt,
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
                    float maxPenetration) {
    vel.x = moveInputX * moveSpeed;
    vel.z = 0.0f;
    vel.y -= gravity * dt;
    if (jumpPressed && grounded) {
        vel.y = jumpSpeed;
        grounded = false;
    }

    auto moveAxis = [&](int axis) {
        float delta = vel[axis] * dt;
        if (delta == 0.0f) return;

        float dir = (delta > 0.0f) ? 1.0f : -1.0f;
        float move = delta;

        glm::vec3 pMin = pos - halfExtents;
        glm::vec3 pMax = pos + halfExtents;

        float bestMove = move;

        if (!colMins || !colMaxs) {
            pos[axis] += move;
            return;
        }

        for (size_t i = 0; i < colMins->size(); ++i) {
            const glm::vec3& cMin = (*colMins)[i];
            const glm::vec3& cMax = (*colMaxs)[i];

            if (axis == 0) {
                if (pMax.y <= cMin.y || pMin.y >= cMax.y) continue;
                if (pMax.z <= cMin.z || pMin.z >= cMax.z) continue;
            } else if (axis == 1) {
                if (pMax.x <= cMin.x || pMin.x >= cMax.x) continue;
                if (pMax.z <= cMin.z || pMin.z >= cMax.z) continue;
            }

            float dist;
            if (dir > 0.0f) {
                dist = cMin[axis] - pMax[axis] - skin;
            } else {
                dist = cMax[axis] - pMin[axis] + skin;
            }

            if (std::abs(dist) < maxPenetration) {
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

    // Y then X (same as before)
    moveAxis(1);
    moveAxis(0);

    // Lock Z plane
    pos.z = levelMidZ;

    // Grounded check
    grounded = false;
    glm::vec3 pMin = pos - halfExtents;
    glm::vec3 pMax = pos + halfExtents;
    if (colMins && colMaxs) {
        for (size_t i = 0; i < colMins->size(); ++i) {
            const auto& cMin = (*colMins)[i];
            const auto& cMax = (*colMaxs)[i];

            bool overlapXZ =
                pMax.x > cMin.x && pMin.x < cMax.x &&
                pMax.z > cMin.z && pMin.z < cMax.z;

            bool touchingFromAbove =
                std::abs(pMin.y - cMax.y) < 0.05f &&
                vel.y <= 0.0f;

            if (overlapXZ && touchingFromAbove) {
                grounded = true;
                break;
            }
        }
    }

    // Fallback floor if no colliders
    if (!colMins || !colMaxs || colMins->empty()) {
        float floorY = boundsMinY + halfExtents.y;
        if (pos.y < floorY) {
            pos.y = floorY;
            vel.y = 0.0f;
            grounded = true;
        }
    }
}


