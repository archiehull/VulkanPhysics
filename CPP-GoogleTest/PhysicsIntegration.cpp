#include "pch.h"
#include <gtest/gtest.h>
#include "Sphere.h"
#include "PhysicsHelper.h"
#include <glm/glm.hpp>

TEST(PhysicsIntegration, SphereCollisionUpdatesVelocity) {
    // Entity A equivalent: moving right
    MovingSphere a(
        glm::vec3(0.0f, 0.0f, 0.0f),   // position
        1.0f,                           // radius
        glm::vec3(10.0f, 0.0f, 0.0f),  // velocity
        1.0f                            // mass
    );

    // Entity B equivalent: stationary, overlapping
    MovingSphere b(
        glm::vec3(1.5f, 0.0f, 0.0f),   // position (overlap with radius 1 + 1)
        1.0f,                           // radius
        glm::vec3(0.0f, 0.0f, 0.0f),   // velocity
        1.0f                            // mass
    );

    ASSERT_TRUE(a.sphere.CollideWith(b.sphere));

    ResolveElasticCollision(a, b);

    EXPECT_LT(a.velocity.x, 10.0f);
    EXPECT_GT(b.velocity.x, 0.0f);
}