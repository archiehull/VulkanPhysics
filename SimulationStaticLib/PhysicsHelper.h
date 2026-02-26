#pragma once
#include "Sphere.h"
#include "Plane.h"

struct MovingSphere {
    Sphere sphere;
    glm::vec3 velocity;
    float mass;
    float restitution;

    MovingSphere(const glm::vec3& pos, float r, const glm::vec3& vel, float m = 1.0f, float rest = 1.0f)
        : sphere(pos, r), velocity(vel), mass(m), restitution(rest) {
    }
};

inline void ResolveElasticCollision(MovingSphere& a, MovingSphere& b) {
    glm::vec3 normal = b.sphere.Position() - a.sphere.Position();
    float distSq = glm::dot(normal, normal);
    if (distSq == 0.0f) return;

    glm::vec3 relVel = a.velocity - b.velocity;
    float velAlongNormal = glm::dot(relVel, normal);

    if (velAlongNormal < 0.0f) return;

    double e = static_cast<double>(a.restitution) * static_cast<double>(b.restitution);
    double invMassSum = (1.0 / static_cast<double>(a.mass)) + (1.0 / static_cast<double>(b.mass));

    // Use unnormalized collision axis to avoid precision loss from normalization.
    double j = -((1.0 + e) * static_cast<double>(velAlongNormal));
    j /= (invMassSum * static_cast<double>(distSq));

    glm::vec3 impulse = normal * static_cast<float>(j);
    a.velocity += impulse * (1.0f / a.mass);
    b.velocity -= impulse * (1.0f / b.mass);
}

inline void ResolveSpherePlaneCollision(MovingSphere& a, const Plane& p, float planeRestitution) {
    float velAlongNormal = glm::dot(a.velocity, p.GetNormal());

    // If moving away from the plane, do nothing
    if (velAlongNormal > 0.0f) return;

    // Combine restitution (bounciness)
    float e = a.restitution * planeRestitution;
    float j = -(1.0f + e) * velAlongNormal;

    // Mass of plane is infinite, so we only divide by sphere's mass
    j /= (1.0f / a.mass);

    glm::vec3 impulse = p.GetNormal() * j;
    a.velocity += impulse * (1.0f / a.mass);
}

inline float GetKineticEnergy(const MovingSphere& body) {
    return 0.5f * body.mass * glm::dot(body.velocity, body.velocity);
}

inline glm::vec3 GetMomentum(const MovingSphere& body) {
    return body.velocity * body.mass;
}