#include "pch.h"
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include "PhysicsHelper.h"

// Helper macro to compare two 3x3 matrices with a tolerance
#ifndef ExpectMat3Near
#define ExpectMat3Near(m1, m2, tolerance) \
    for (int col = 0; col < 3; ++col) { \
        for (int row = 0; row < 3; ++row) { \
            EXPECT_NEAR((m1)[col][row], (m2)[col][row], tolerance); \
        } \
    }
#endif

// Helper function to simulate time passing at 60 FPS
void SimulateTime(MovingSphere& obj, float totalTime, float dt = 1.0f / 60.0f) {
    int steps = static_cast<int>(std::round(totalTime / dt));
    for (int i = 0; i < steps; ++i) {
        IntegrateAngularVelocity(obj, dt);
    }
}

// ---------------------------------------------------------
// Angular Velocity Integration Tests
// ---------------------------------------------------------

TEST(AngularVelocityTests, Rotate_X_90_PerSec_For_1Sec) {
    MovingSphere obj({0, 0, 0}, 1.0f, {0, 0, 0});
    // 90 degrees (pi/2) per second around X
    obj.angularVelocity = glm::vec3(glm::pi<float>() / 2.0f, 0.0f, 0.0f);

    SimulateTime(obj, 1.0f); // Simulate for 1 second

    // Expected: 90 degree rotation around X
    glm::mat3 expected(
        1.0f,  0.0f,  0.0f,
        0.0f,  0.0f,  1.0f,
        0.0f, -1.0f,  0.0f
    );
    ExpectMat3Near(obj.orientation, expected, 1e-3f);
}

TEST(AngularVelocityTests, Rotate_Y_180_PerSec_For_2Sec) {
    MovingSphere obj({0, 0, 0}, 1.0f, {0, 0, 0});
    // 180 degrees (pi) per second around Y
    obj.angularVelocity = glm::vec3(0.0f, glm::pi<float>(), 0.0f);

    SimulateTime(obj, 2.0f); // Simulate for 2 seconds

    // Expected: 360 degree rotation around Y (Identity Matrix)
    glm::mat3 expected(
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    );
    ExpectMat3Near(obj.orientation, expected, 1e-3f);
}

TEST(AngularVelocityTests, Rotate_Z_270_PerSec_For_3Sec) {
    MovingSphere obj({0, 0, 0}, 1.0f, {0, 0, 0});
    // 270 degrees (1.5*pi) per second around Z
    obj.angularVelocity = glm::vec3(0.0f, 0.0f, 1.5f * glm::pi<float>());

    SimulateTime(obj, 3.0f); // Simulate for 3 seconds

    // 270 deg/s * 3s = 810 degrees. 810 % 360 = 90 degrees around Z
    glm::mat3 expected(
         0.0f, 1.0f, 0.0f,
        -1.0f, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f
    );
    ExpectMat3Near(obj.orientation, expected, 1e-3f);
}

TEST(AngularVelocityTests, Rotate_Combo_XY_For_4Sec) {
    MovingSphere obj({0, 0, 0}, 1.0f, {0, 0, 0});
    
    // Rotate 90 deg/sec on a diagonal axis (combination of X and Y)
    // We want the total magnitude to be pi/2 per second.
    glm::vec3 axis = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    obj.angularVelocity = axis * (glm::pi<float>() / 2.0f);

    SimulateTime(obj, 4.0f); // Simulate for 4 seconds

    // 90 deg/s * 4s = 360 degrees (Identity Matrix expected)
    glm::mat3 expected(
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    );
    // Use slightly higher tolerance due to floating point drift over 4 seconds of integration
    ExpectMat3Near(obj.orientation, expected, 1e-2f);
}

TEST(AngularVelocityTests, SphereSphere_Glancing_WithFriction_GeneratesSpin) {
    MovingSphere a({ 0.0f, 0.0f, 0.0f }, 1.0f, { 6.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
    MovingSphere b({ 1.2f, 1.6f, 0.0f }, 1.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f);

    ASSERT_TRUE(a.sphere.CollideWith(b.sphere));
    ResolveElasticCollision(a, b, false, 0.0f, 1.0f);

    EXPECT_GT(glm::length(a.angularVelocity), 1e-5f);
    EXPECT_GT(glm::length(b.angularVelocity), 1e-5f);
}

TEST(AngularVelocityTests, SphereSphere_Glancing_ZeroFriction_NoSpinGenerated) {
    MovingSphere a({ 0.0f, 0.0f, 0.0f }, 1.0f, { 6.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
    MovingSphere b({ 1.2f, 1.6f, 0.0f }, 1.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f);

    ASSERT_TRUE(a.sphere.CollideWith(b.sphere));
    ResolveElasticCollision(a, b, false, 0.0f, 0.0f);

    EXPECT_NEAR(glm::length(a.angularVelocity), 0.0f, 1e-6f);
    EXPECT_NEAR(glm::length(b.angularVelocity), 0.0f, 1e-6f);
}

TEST(AngularVelocityTests, SpherePlane_WithTangentialSpeed_GeneratesSpin) {
    MovingSphere a({ 0.0f, 0.9f, 0.0f }, 1.0f, { 4.0f, -2.0f, 0.0f }, 1.0f, 1.0f);
    Plane floor({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

    ASSERT_TRUE(floor.Intersects(a.sphere));

    const float beforeTangential = std::abs(a.velocity.x);
    ResolveSpherePlaneCollision(a, floor, 1.0f, 1.0f);

    EXPECT_GT(glm::length(a.angularVelocity), 1e-5f);
    EXPECT_LT(std::abs(a.velocity.x), beforeTangential);
}

TEST(AngularVelocityTests, SphereSphere_ForceMode_AccumulatesTorque) {
    MovingSphere a({ 0.0f, 0.0f, 0.0f }, 1.0f, { 6.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
    MovingSphere b({ 1.2f, 1.6f, 0.0f }, 1.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f);

    ASSERT_TRUE(a.sphere.CollideWith(b.sphere));

    const glm::vec3 aVelBefore = a.velocity;
    const glm::vec3 bVelBefore = b.velocity;

    ResolveElasticCollision(a, b, true, 1.0f / 60.0f, 1.0f);

    EXPECT_NEAR(glm::length(a.velocity - aVelBefore), 0.0f, 1e-6f);
    EXPECT_NEAR(glm::length(b.velocity - bVelBefore), 0.0f, 1e-6f);
    EXPECT_GT(glm::length(a.torqueAccumulator), 1e-6f);
    EXPECT_GT(glm::length(b.torqueAccumulator), 1e-6f);
}

TEST(AngularVelocityTests, ContactFrictionFormula_IsCentralizedInStaticLib) {
    EXPECT_NEAR(ComputeContactFriction(0.4f, 0.6f, 2.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(ComputeContactFriction(0.4f, 0.6f, 1.0f), 0.5f, 1e-6f);
    EXPECT_NEAR(ComputeContactFriction(0.0f, 1.0f, 1.0f), 0.5f, 1e-6f);
}

TEST(AngularVelocityTests, SpherePlane_BackspinAffectsBounce) {
    MovingSphere ball({ 0.0f, 1.1f, 0.0f }, 1.0f, { 0.0f, -5.0f, 0.0f }, 1.0f, 1.0f);
    // Add backspin: angular velocity around Z-axis (assuming Y-up)
    ball.angularVelocity = glm::vec3(0.0f, 0.0f, 10.0f); // Backspin

    Plane floor({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

    ASSERT_TRUE(floor.Intersects(ball.sphere));

    const float initialYVel = ball.velocity.y;
    ResolveSpherePlaneCollision(ball, floor, 1.0f, 1.0f);

    // With backspin, the ball should bounce higher due to friction pushing it up
    EXPECT_GT(ball.velocity.y, -initialYVel); // Should bounce up more than just reversing
    EXPECT_NE(ball.angularVelocity.z, 10.0f); // Angular velocity should change due to friction
}

TEST(AngularVelocityTests, SpherePlane_NoSpin_NoExtraBounce) {
    MovingSphere ball({ 0.0f, 1.1f, 0.0f }, 1.0f, { 0.0f, -5.0f, 0.0f }, 1.0f, 1.0f);
    // No spin
    ball.angularVelocity = glm::vec3(0.0f, 0.0f, 0.0f);

    Plane floor({ 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

    ASSERT_TRUE(floor.Intersects(ball.sphere));

    ResolveSpherePlaneCollision(ball, floor, 1.0f, 1.0f);

    // Without spin, should just reverse Y velocity approximately
    EXPECT_NEAR(ball.velocity.y, 5.0f, 0.1f);
    EXPECT_NEAR(ball.angularVelocity.z, 0.0f, 1e-6f);
}

TEST(AngularVelocityTests, SphereSphere_SpinTransferOnCollision) {
    MovingSphere a({ 0.0f, 0.0f, 0.0f }, 1.0f, { 5.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
    a.angularVelocity = glm::vec3(0.0f, 0.0f, 5.0f); // Spinning

    MovingSphere b({ 2.0f, 0.0f, 0.0f }, 1.0f, { 0.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
    b.angularVelocity = glm::vec3(0.0f, 0.0f, 0.0f); // Stationary

    ASSERT_TRUE(a.sphere.CollideWith(b.sphere));

    const glm::vec3 aAngBefore = a.angularVelocity;
    const glm::vec3 bAngBefore = b.angularVelocity;

    ResolveElasticCollision(a, b, false, 0.0f, 1.0f);

    // Spin should transfer
    EXPECT_NE(a.angularVelocity.z, aAngBefore.z);
    EXPECT_NE(b.angularVelocity.z, bAngBefore.z);
}
