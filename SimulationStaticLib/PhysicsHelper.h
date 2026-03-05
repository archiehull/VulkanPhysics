#pragma once
#include "Sphere.h"
#include "Plane.h"

// SimulationStaticLib/PhysicsHelper.h

struct MovingSphere {
    Sphere sphere;
    glm::vec3 velocity;
    glm::vec3 forceAccumulator;
    float invMass;
    float restitution;

    MovingSphere(const glm::vec3& pos, float r, const glm::vec3& vel, float invM = 1.0f, float rest = 1.0f)
        : sphere(pos, r), velocity(vel), forceAccumulator(0.0f), invMass(invM), restitution(rest) {
    }
};

inline void ResolveElasticCollision(MovingSphere& a, MovingSphere& b, bool useForce = false, float dt = 0.0f) {
    glm::vec3 normal = b.sphere.Position() - a.sphere.Position();
    float distSq = glm::dot(normal, normal);
    if (distSq == 0.0f) return;

    glm::vec3 relVel = a.velocity - b.velocity;
    float velAlongNormal = glm::dot(relVel, normal);
    if (velAlongNormal < 0.0f) return;

    double e = static_cast<double>(a.restitution) * static_cast<double>(b.restitution);
    double invMassSum = static_cast<double>(a.invMass) + static_cast<double>(b.invMass);
    if (invMassSum <= 0.0) return;

    double j = -((1.0 + e) * static_cast<double>(velAlongNormal));
    j /= (invMassSum * static_cast<double>(distSq));

    glm::vec3 impulse = normal * static_cast<float>(j);

    if (useForce && dt > 0.0f) {
        // Force Accumulation Approach: F = m * (dv / dt)
        // Since impulse = dv * m, F = impulse / dt
        glm::vec3 force = impulse / dt;

        // Apply equal and opposite forces
        if (a.invMass > 0.0f) a.forceAccumulator += force;
        if (b.invMass > 0.0f) b.forceAccumulator -= force;
    }
    else {
        // Direct Velocity Change (Impulse) Approach
        a.velocity += impulse * a.invMass;
        b.velocity -= impulse * b.invMass;
    }
}

// planeRestitution: restitution from plane body
// contactFriction: [0..1], where 1 = keep tangential speed, 0 = kill tangential speed
inline void ResolveSpherePlaneCollision(
    MovingSphere& a,
    const Plane& p,
    float planeRestitution,
    float contactFriction = 1.0f)
{
    const glm::vec3 n = p.GetNormal();
    const float vn = glm::dot(a.velocity, n);

    // Moving away from plane
    if (vn >= 0.0f) return;

    const float e = glm::clamp(a.restitution * planeRestitution, 0.0f, 1.0f);

    // Bounce (mass-independent for infinite-mass plane)
    a.velocity -= (1.0f + e) * vn * n;

    // Tangential damping (friction)
    const float tangentDamping = glm::clamp(contactFriction, 0.0f, 1.0f);
    const glm::vec3 vNormal = glm::dot(a.velocity, n) * n;
    const glm::vec3 vTangent = a.velocity - vNormal;
    a.velocity = vNormal + (vTangent * tangentDamping);
}

// Update to handle potential infinite mass
inline float GetKineticEnergy(const MovingSphere& body) {
    if (body.invMass <= 0.0f) return 0.0f; // Infinite mass objects don't "possess" KE in this context
    return 0.5f * (1.0f / body.invMass) * glm::dot(body.velocity, body.velocity);
}

inline glm::vec3 GetMomentum(const MovingSphere& body) {
    if (body.invMass <= 0.0f) return glm::vec3(0.0f);

    return body.velocity * (1.0f / body.invMass);
}