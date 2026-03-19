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

TEST(OrientationTests, Rotate_X_90) {
    MovingSphere obj({0.0f, 0.0f, 0.0f}, 1.0f, {0.0f, 0.0f, 0.0f});
    float angle = glm::pi<float>() / 2.0f; // 90 degrees

    ApplyAngularDisplacement(obj, glm::vec3(1.0f, 0.0f, 0.0f), angle);

    glm::mat3 expected(
        1.0f,  0.0f,  0.0f,  // Column 0
        0.0f,  0.0f,  1.0f,  // Column 1
        0.0f, -1.0f,  0.0f   // Column 2
    );

    ExpectMat3Near(obj.orientation, expected, 1e-5f);
}

TEST(OrientationTests, Rotate_Y_180) {
    MovingSphere obj({0.0f, 0.0f, 0.0f}, 1.0f, {0.0f, 0.0f, 0.0f});
    float angle = glm::pi<float>(); // 180 degrees

    ApplyAngularDisplacement(obj, glm::vec3(0.0f, 1.0f, 0.0f), angle);

    glm::mat3 expected(
       -1.0f,  0.0f,  0.0f,
        0.0f,  1.0f,  0.0f,
        0.0f,  0.0f, -1.0f
    );

    ExpectMat3Near(obj.orientation, expected, 1e-5f);
}

TEST(OrientationTests, Rotate_Z_270) {
    MovingSphere obj({0.0f, 0.0f, 0.0f}, 1.0f, {0.0f, 0.0f, 0.0f});
    float angle = 1.5f * glm::pi<float>(); // 270 degrees

    ApplyAngularDisplacement(obj, glm::vec3(0.0f, 0.0f, 1.0f), angle);

    glm::mat3 expected(
        0.0f, -1.0f,  0.0f,
        1.0f,  0.0f,  0.0f,
        0.0f,  0.0f,  1.0f
    );

    ExpectMat3Near(obj.orientation, expected, 1e-4f);
}

TEST(OrientationTests, Rotate_XY_Combination) {
    MovingSphere obj({0.0f, 0.0f, 0.0f}, 1.0f, {0.0f, 0.0f, 0.0f});
    float angle90 = glm::pi<float>() / 2.0f;

    // Rotate 90 around X, then 90 around Y
    ApplyAngularDisplacement(obj, glm::vec3(1.0f, 0.0f, 0.0f), angle90);
    ApplyAngularDisplacement(obj, glm::vec3(0.0f, 1.0f, 0.0f), angle90);

    glm::mat3 expected(
        0.0f,  0.0f, -1.0f,
        1.0f,  0.0f,  0.0f,
        0.0f, -1.0f,  0.0f
    );

    ExpectMat3Near(obj.orientation, expected, 1e-5f);
}
