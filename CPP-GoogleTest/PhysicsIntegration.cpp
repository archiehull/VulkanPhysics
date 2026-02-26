#include "pch.h"
#include <gtest/gtest.h>
#include "Sphere.h"
#include "PhysicsHelper.h"
#include <glm/glm.hpp>

// Helper macro to compare glm vectors
#define ExpectVec3Near(a, b, tolerance) \
    EXPECT_NEAR((a).x, (b).x, tolerance); \
    EXPECT_NEAR((a).y, (b).y, tolerance); \
    EXPECT_NEAR((a).z, (b).z, tolerance)


// -----------------------------------------------------------------------------
// Existing Test: Collision
// -----------------------------------------------------------------------------
TEST(PhysicsIntegration, SphereCollisionUpdatesVelocity) {
    MovingSphere a(glm::vec3(0.0f), 1.0f, glm::vec3(10.0f, 0.0f, 0.0f), 1.0f);
    MovingSphere b(glm::vec3(1.5f, 0.0f, 0.0f), 1.0f, glm::vec3(0.0f), 1.0f);

    ASSERT_TRUE(a.sphere.CollideWith(b.sphere));
    ResolveElasticCollision(a, b);

    EXPECT_LT(a.velocity.x, 10.0f);
    EXPECT_GT(b.velocity.x, 0.0f);
}


// -----------------------------------------------------------------------------
// Q2 Tests: Zero Acceleration Integration (x = x0 + v*t)
// -----------------------------------------------------------------------------

TEST(PhysicsIntegration, ExplicitEuler_ZeroAcceleration) {
    glm::vec3 position(0.0f, 0.0f, 0.0f);
    glm::vec3 velocity(5.0f, 2.0f, 0.0f);
    glm::vec3 acceleration(0.0f, 0.0f, 0.0f);

    float dt = 0.016f;
    float totalTime = 2.0f;
    int steps = static_cast<int>(totalTime / dt);
    float actualSimulatedTime = steps * dt; // Prevents float truncation mismatches

    for (int i = 0; i < steps; ++i) {
        position += velocity * dt;
        velocity += acceleration * dt;
    }

    // Exact formula: x = x0 + v*t
    glm::vec3 expectedPosition = glm::vec3(0.0f) + (glm::vec3(5.0f, 2.0f, 0.0f) * actualSimulatedTime);

    // Should be perfectly accurate
    ExpectVec3Near(position, expectedPosition, 1e-4f);
}

TEST(PhysicsIntegration, SemiImplicitEuler_ZeroAcceleration) {
    glm::vec3 position(10.0f, 5.0f, 0.0f);
    glm::vec3 velocity(-3.0f, 0.0f, 1.0f);
    glm::vec3 acceleration(0.0f, 0.0f, 0.0f);

    float dt = 0.01f;
    float totalTime = 5.0f;
    int steps = static_cast<int>(totalTime / dt);
    float actualSimulatedTime = steps * dt;

    for (int i = 0; i < steps; ++i) {
        velocity += acceleration * dt;
        position += velocity * dt;
    }

    glm::vec3 expectedPosition = glm::vec3(10.0f, 5.0f, 0.0f) + (glm::vec3(-3.0f, 0.0f, 1.0f) * actualSimulatedTime);

    ExpectVec3Near(position, expectedPosition, 1e-4f);
}


// -----------------------------------------------------------------------------
// Q3 Tests: Constant Acceleration & Error Bounds (x = x0 + v0*t + 0.5*a*t^2)
// -----------------------------------------------------------------------------

TEST(PhysicsIntegration, ExplicitEuler_WithGravity_ShowsError) {
    glm::vec3 position(0.0f, 100.0f, 0.0f);
    glm::vec3 velocity(0.0f, 0.0f, 0.0f);
    glm::vec3 force(0.0f, -9.81f, 0.0f);
    float inverseMass = 1.0f;

    float dt = 0.016f;
    float totalTime = 2.0f;
    int steps = static_cast<int>(totalTime / dt);
    float actualSimulatedTime = steps * dt;

    for (int i = 0; i < steps; ++i) {
        glm::vec3 acceleration = force * inverseMass;
        position += velocity * dt;
        velocity += acceleration * dt;
    }

    // Exact kinematic formula: x = x0 + v0*t + 0.5*a*t^2
    glm::vec3 exactPos = glm::vec3(0.0f, 100.0f, 0.0f) +
        (0.5f * glm::vec3(0.0f, -9.81f, 0.0f) * (actualSimulatedTime * actualSimulatedTime));

    // Prove that Euler has error! 
    // We expect it to NOT be exactly equal, but within a loose tolerance (0.5 units).
    EXPECT_NE(position.y, exactPos.y);
    EXPECT_NEAR(position.y, exactPos.y, 0.5f);
}

TEST(PhysicsIntegration, RK4_PerfectAccuracy_WithGravity) {
    glm::vec3 position(0.0f, 100.0f, 0.0f);
    glm::vec3 velocity(0.0f, 0.0f, 0.0f);
    glm::vec3 force(0.0f, -9.81f, 0.0f);
    float inverseMass = 1.0f;

    float dt = 0.016f;
    float totalTime = 2.0f;
    int steps = static_cast<int>(totalTime / dt);
    float actualSimulatedTime = steps * dt;

    for (int i = 0; i < steps; ++i) {
        glm::vec3 a = force * inverseMass;

        // RK4 Integration (Standalone for static lib test)
        glm::vec3 k1_v = a, k1_x = velocity;
        glm::vec3 k2_v = a, k2_x = velocity + k1_v * (dt * 0.5f);
        glm::vec3 k3_v = a, k3_x = velocity + k2_v * (dt * 0.5f);
        glm::vec3 k4_v = a, k4_x = velocity + k3_v * dt;

        velocity += (k1_v + 2.0f * k2_v + 2.0f * k3_v + k4_v) * (dt / 6.0f);
        position += (k1_x + 2.0f * k2_x + 2.0f * k3_x + k4_x) * (dt / 6.0f);
    }

    // Exact kinematic formula
    glm::vec3 exactPos = glm::vec3(0.0f, 100.0f, 0.0f) +
        (0.5f * glm::vec3(0.0f, -9.81f, 0.0f) * (actualSimulatedTime * actualSimulatedTime));

    // RK4 handles constant acceleration perfectly! Tolerance is microscopic.
    ExpectVec3Near(position, exactPos, 1e-4f);
}