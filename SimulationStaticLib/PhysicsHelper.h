#pragma once
#include "Sphere.h"
#include "Plane.h"

struct MovingSphere {
    Sphere sphere;
    glm::vec3 velocity;
    glm::vec3 forceAccumulator;
    float invMass;
    float restitution;

    MovingSphere(const glm::vec3& pos, float r, const glm::vec3& vel, float invM = 1.0f, float rest = 1.0f);
};

void ResolveElasticCollision(MovingSphere& a, MovingSphere& b, bool useForce = false, float dt = 0.0f);

void ResolveSpherePlaneCollision(
    MovingSphere& a,
    const Plane& p,
    float planeRestitution,
    float contactFriction = 1.0f);

float GetKineticEnergy(const MovingSphere& body);

glm::vec3 GetMomentum(const MovingSphere& body);

// === NEW COMPREHENSIVE PHYSICS FUNCTIONS ===

// Apply impulse along a direction
void ApplyImpulse(MovingSphere& body, const glm::vec3& impulse);

// Apply force (integrated over time)
void ApplyForce(MovingSphere& body, const glm::vec3& force);

// Calculate total system energy (useful for energy conservation checks)
float GetTotalSystemEnergy(const MovingSphere* bodies, int count);

// Calculate total system momentum
glm::vec3 GetTotalSystemMomentum(const MovingSphere* bodies, int count);

// Calculate relative velocity at collision point
glm::vec3 GetRelativeVelocity(const MovingSphere& a, const MovingSphere& b);

// Calculate coefficient of restitution from velocities
float CalculateRestitutionFromVelocities(const glm::vec3& v1Before, const glm::vec3& v1After, const glm::vec3& normal);

// Damping/drag utilities
void ApplyLinearDamping(MovingSphere& body, float damping, float dt);
void ApplyQuadraticDrag(MovingSphere& body, float dragCoefficient, float dt);