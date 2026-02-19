#pragma once
#include "Sphere.h"

struct MovingSphere {
    Sphere sphere;
    glm::vec3 velocity;
    float mass;

    MovingSphere(const glm::vec3& pos, float r, const glm::vec3& vel, float m = 1.0f)
        : sphere(pos, r), velocity(vel), mass(m) {
    }
};

inline void ResolveElasticCollision(MovingSphere& a, MovingSphere& b) {
    glm::vec3 normal = b.sphere.Position() - a.sphere.Position();
    float distSq = glm::length2(normal);
    if (distSq == 0.0f) return;

    normal = glm::normalize(normal);
    glm::vec3 relVel = a.velocity - b.velocity;
    float velAlongNormal = glm::dot(relVel, normal);

    if (velAlongNormal < 0) return;

    float e = 1.0f;
    float j = -(1.0f + e) * velAlongNormal;
    j /= (1.0f / a.mass + 1.0f / b.mass);

    glm::vec3 impulse = normal * j;
    a.velocity += impulse * (1.0f / a.mass);
    b.velocity -= impulse * (1.0f / b.mass);
}