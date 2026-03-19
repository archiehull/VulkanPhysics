#include "pch.h"
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
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
