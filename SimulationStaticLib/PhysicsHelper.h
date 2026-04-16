#pragma once
#include "Sphere.h"
#include "Plane.h"
#include <glm/gtc/matrix_transform.hpp>

struct MovingSphere {
    Sphere sphere;
    glm::vec3 velocity;
    glm::vec3 forceAccumulator;
    float invMass;
    float restitution;

    // Orientation stored as a 3x3 rotation matrix (column-major)
    glm::mat3 orientation = glm::mat3(1.0f);

    // NEW: Angular velocity in radians/sec around local axis
    glm::vec3 angularVelocity = glm::vec3(0.0f);

    // NEW: Torque & Inertia (body-space)
    glm::vec3 torqueAccumulator = glm::vec3(0.0f);
    glm::mat3 inertiaTensor = glm::mat3(1.0f);
    glm::mat3 inverseInertiaTensor = glm::mat3(1.0f);

    MovingSphere(const glm::vec3& pos, float r, const glm::vec3& vel, float invM = 1.0f, float rest = 1.0f);
};

void ResolveElasticCollision(MovingSphere& a, MovingSphere& b, bool useForce = false, float dt = 0.0f, float friction = 0.5f);

void ResolveSpherePlaneCollision(
    MovingSphere& a,
    const Plane& p,
    float planeRestitution,
    float contactFriction = 1.0f);

float ComputeContactFriction(float frictionA, float frictionB, float globalScale = 1.0f);

float GetKineticEnergy(const MovingSphere& body);

glm::vec3 GetMomentum(const MovingSphere& body);

// === NEW COMPREHENSIVE PHYSICS FUNCTIONS ===

// Apply impulse along a direction
void ApplyImpulse(MovingSphere& body, const glm::vec3& impulse);

// Apply force (integrated over time)
void ApplyForce(MovingSphere& body, const glm::vec3& force);

// Apply a force at a world-space point (generates linear force + torque)
void ApplyForceAtPoint(MovingSphere& body, const glm::vec3& force, const glm::vec3& point);

float GetTotalSystemEnergy(const MovingSphere* bodies, int count);

glm::vec3 GetTotalSystemMomentum(const MovingSphere* bodies, int count);

glm::vec3 GetRelativeVelocity(const MovingSphere& a, const MovingSphere& b);

float CalculateRestitutionFromVelocities(const glm::vec3& v1Before, const glm::vec3& v1After, const glm::vec3& normal);

void ApplyLinearDamping(MovingSphere& body, float damping, float dt);
void ApplyQuadraticDrag(MovingSphere& body, float dragCoefficient, float dt);

void ApplyAngularDisplacement(MovingSphere& body, const glm::vec3& axis, float angleRadians);

void IntegrateAngularVelocity(MovingSphere& body, float dt);
