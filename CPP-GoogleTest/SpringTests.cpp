#include "pch.h"
#include <gtest/gtest.h>
#include "../src/core/Components.h"
#include <glm/glm.hpp>

#ifndef ExpectVec3Near
#define ExpectVec3Near(a, b, tol) \
    EXPECT_NEAR((a).x, (b).x, tol); \
    EXPECT_NEAR((a).y, (b).y, tol); \
    EXPECT_NEAR((a).z, (b).z, tol)
#endif

TEST(SpringComponent_Physics, AtRest_NoForceApplied) {
    // Setup components
    TransformComponent tA; tA.position = glm::vec3(0.0f);
    TransformComponent tB; tB.position = glm::vec3(1.0f, 0.0f, 0.0f);

    PhysicsComponent pA; pA.isStatic = false; pA.SetMass(1.0f);
    PhysicsComponent pB; pB.isStatic = false; pB.SetMass(1.0f);

    SpringComponent spring;
    spring.restingLength = 1.0f;
    spring.stiffness = 10.0f;
    spring.damping = 1.0f;
    spring.isAttachedToEntity = true;

    // Calculate expected forces using same formulas as system
    glm::vec3 posA = tA.position;
    glm::vec3 posB = tB.position;
    glm::vec3 delta = posB - posA;
    float distance = glm::length(delta);
    ASSERT_GT(distance, 0.0f);
    glm::vec3 dir = delta / distance;

    float displacement = distance - spring.restingLength; // should be 0
    float springForceMagnitude = spring.stiffness * displacement;
    glm::vec3 springForce = dir * springForceMagnitude;

    glm::vec3 velA = pA.velocity;
    glm::vec3 velB = pB.velocity;
    glm::vec3 relativeVel = velB - velA;
    float velocityAlong = glm::dot(relativeVel, dir);
    float dampingForceMagnitude = spring.damping * velocityAlong;
    glm::vec3 dampingForce = dir * dampingForceMagnitude;

    glm::vec3 total = springForce + dampingForce;

    // Apply
    pA.ApplyForce(total);
    pB.ApplyForce(-total);

    ExpectVec3Near(pA.forceAccumulator, glm::vec3(0.0f), 1e-5f);
    ExpectVec3Near(pB.forceAccumulator, glm::vec3(0.0f), 1e-5f);
}

TEST(SpringComponent_Physics, SpringAndDamping_ApplyOppositeForces) {
    TransformComponent tA; tA.position = glm::vec3(0.0f);
    TransformComponent tB; tB.position = glm::vec3(2.0f, 0.0f, 0.0f);

    PhysicsComponent pA; pA.isStatic = false; pA.SetMass(1.0f);
    PhysicsComponent pB; pB.isStatic = false; pB.SetMass(1.0f);

    pA.velocity = glm::vec3(0.0f);
    pB.velocity = glm::vec3(-1.0f, 0.0f, 0.0f);

    SpringComponent spring;
    spring.restingLength = 1.0f;
    spring.stiffness = 10.0f;
    spring.damping = 2.0f;
    spring.isAttachedToEntity = true;

    glm::vec3 posA = tA.position;
    glm::vec3 posB = tB.position;
    glm::vec3 delta = posB - posA;
    float distance = glm::length(delta);
    ASSERT_GT(distance, 0.0f);
    glm::vec3 dir = delta / distance;

    float displacement = distance - spring.restingLength; // 1.0
    float springForceMagnitude = spring.stiffness * displacement; // 10
    glm::vec3 springForce = dir * springForceMagnitude; // (10,0,0)

    glm::vec3 relativeVel = pB.velocity - pA.velocity; // (-1,0,0)
    float velocityAlong = glm::dot(relativeVel, dir); // -1
    float dampingForceMagnitude = spring.damping * velocityAlong; // -2
    glm::vec3 dampingForce = dir * dampingForceMagnitude; // (-2,0,0)

    glm::vec3 total = springForce + dampingForce; // (8,0,0)

    pA.ApplyForce(total);
    pB.ApplyForce(-total);

    ExpectVec3Near(pA.forceAccumulator, glm::vec3(8.0f, 0.0f, 0.0f), 1e-5f);
    ExpectVec3Near(pB.forceAccumulator, glm::vec3(-8.0f, 0.0f, 0.0f), 1e-5f);
}
