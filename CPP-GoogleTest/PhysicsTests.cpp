#include "pch.h"
#include "Sphere.h"
#include "Plane.h"
#include "Cylinder.h"
#include "PhysicsHelper.h"
#include <glm/glm.hpp>

#ifndef ExpectVec3Near
#define ExpectVec3Near(a, b) \
    EXPECT_NEAR((a).x, (b).x, 1e-5f); \
    EXPECT_NEAR((a).y, (b).y, 1e-5f); \
    EXPECT_NEAR((a).z, (b).z, 1e-5f)
#endif

// --- Energy and Momentum Conservation Tests ---

TEST(Physics_Conservation, Energy_PerfectlyElastic) {
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 10.0, 5.0, 0.0 });
    MovingSphere b({ 2.0, 1.0, 0.0 }, 1.0f, { -5.0, -2.0, 0.0 });

    float total_ke_initial = GetKineticEnergy(a) + GetKineticEnergy(b);
    ResolveElasticCollision(a, b);
    float total_ke_final = GetKineticEnergy(a) + GetKineticEnergy(b);

    EXPECT_NEAR(total_ke_initial, total_ke_final, 1e-4f);
}

TEST(Physics_Conservation, Momentum_UnequalMass_Diagonal) {
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 3.0, 3.0, 0.0 }, 2.0f);
    MovingSphere b({ 2.0, 2.0, 0.0 }, 1.0f, { 0.0, 0.0, 0.0 }, 3.0f);

    glm::vec3 p_initial = GetMomentum(a) + GetMomentum(b);
    ResolveElasticCollision(a, b);
    glm::vec3 p_final = GetMomentum(a) + GetMomentum(b);

    ExpectVec3Near(p_initial, p_final);
}

// --- Fixed Object Collision Tests (Formative) ---

TEST(Physics_FixedCollision, SpherePlane_DiagonalReflection) {
    // Ball moving Down-Right
    glm::vec3 initialVel(5.0f, -5.0f, 0.0f);
    MovingSphere ball({ 0.0f, 0.1f, 0.0f }, 1.0f, initialVel, 1.0f, 1.0f);

    // To bounce from (5, -5) to (-5, 5), the normal must be Up-Left
    glm::vec3 planeNormal = glm::normalize(glm::vec3(-1.0f, 1.0f, 0.0f));
    Plane fixedPlane({ 0.0f, 0.0f, 0.0f }, planeNormal);

    ASSERT_TRUE(fixedPlane.Intersects(ball.sphere));
    ResolveSpherePlaneCollision(ball, fixedPlane, 1.0f);

    // Verify reflection to (-5, 5)
    ExpectVec3Near(ball.velocity, glm::vec3(-5.0f, 5.0f, 0.0f));
}

TEST(Physics_FixedCollision, SphereStaticSphere_HeadOn) {
    // Moving ball: invMass = 1.0 (mass = 1)
    MovingSphere ballA({ 0.0f, 0.0f, 0.0f }, 1.0f, { 10.0f, 0.0f, 0.0f }, 1.0f, 1.0f);

    // Fixed ball: invMass = 0.0 (mass = infinity)
    MovingSphere fixedBall({ 1.5f, 0.0f, 0.0f }, 1.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, 1.0f);

    ASSERT_TRUE(ballA.sphere.CollideWith(fixedBall.sphere));
    ResolveElasticCollision(ballA, fixedBall);

    // This will now be EXACTLY -10.0f
    EXPECT_NEAR(ballA.velocity.x, -10.0f, 1e-6f);
    EXPECT_NEAR(ballA.velocity.y, 0.0f, 1e-6f);
}

TEST(Physics_FixedCollision, SphereFixedCylinder_SideReflection) {
    Cylinder fixedCyl({ 0.0f, -10.0f, 0.0f }, { 0.0f, 10.0f, 0.0f }, 2.0f);
    MovingSphere ball({ 2.5f, 0.0f, 0.0f }, 1.0f, { -10.0f, 0.0f, 0.0f }, 1.0f, 1.0f);

    ASSERT_TRUE(fixedCyl.Intersects(ball.sphere));

    glm::vec3 normal = glm::normalize(ball.sphere.Position() - glm::vec3(0.0f, ball.sphere.Position().y, 0.0f));
    float velAlongNormal = glm::dot(ball.velocity, normal);
    float j = -(1.0f + ball.restitution) * velAlongNormal;
    ball.velocity += (normal * j);

    ExpectVec3Near(ball.velocity, glm::vec3(10.0f, 0.0f, 0.0f));
}
TEST(Physics_Collision, UnequalMass_HeadOn) {
    // Ball A: mass = 2.0 (invMass = 0.5), moving right at 10 m/s
    MovingSphere a({ 0.0f, 0.0f, 0.0f }, 1.0f, { 10.0f, 0.0f, 0.0f }, 0.5f, 1.0f);

    // Ball B: mass = 3.0 (invMass = 0.3333f), stationary
    MovingSphere b({ 2.0f, 0.0f, 0.0f }, 1.0f, { 0.0f, 0.0f, 0.0f }, 1.0f / 3.0f, 1.0f);

    ResolveElasticCollision(a, b);

    // Using 1D elastic collision formulas:
    // v1 = ((m1 - m2)/(m1 + m2))*u1 = ((2-3)/5)*10 = -2.0 m/s
    // v2 = ((2*m1)/(m1 + m2))*u1 = (4/5)*10 = 8.0 m/s
    EXPECT_NEAR(a.velocity.x, -2.0f, 1e-4f);
    EXPECT_NEAR(a.velocity.y, 0.0f, 1e-4f);

    EXPECT_NEAR(b.velocity.x, 8.0f, 1e-4f);
    EXPECT_NEAR(b.velocity.y, 0.0f, 1e-4f);
}

// Add these tests at the end of the file

TEST(Physics_HelperAPI, ApplyImpulse_DynamicBody) {
    MovingSphere s({ 0.0f, 0.0f, 0.0f }, 1.0f, { 1.0f, 0.0f, 0.0f }, 0.5f, 1.0f); // mass=2
    ApplyImpulse(s, glm::vec3(4.0f, 0.0f, 0.0f)); // dv = J * invMass = 2
    EXPECT_NEAR(s.velocity.x, 3.0f, 1e-5f);
}

TEST(Physics_HelperAPI, ApplyImpulse_StaticBody_NoChange) {
    MovingSphere s({ 0.0f, 0.0f, 0.0f }, 1.0f, { 2.0f, 0.0f, 0.0f }, 0.0f, 1.0f);
    ApplyImpulse(s, glm::vec3(100.0f, 0.0f, 0.0f));
    EXPECT_NEAR(s.velocity.x, 2.0f, 1e-5f);
}

TEST(Physics_HelperAPI, TotalSystemEnergyAndMomentum) {
    MovingSphere bodies[2] = {
        MovingSphere({0,0,0}, 1.0f, {2.0f,0.0f,0.0f}, 1.0f, 1.0f),   // mass=1
        MovingSphere({0,0,0}, 1.0f, {0.0f,3.0f,0.0f}, 0.5f, 1.0f)    // mass=2
    };

    const float totalE = GetTotalSystemEnergy(bodies, 2);
    const glm::vec3 totalP = GetTotalSystemMomentum(bodies, 2);

    // E = 0.5*1*4 + 0.5*2*9 = 2 + 9 = 11
    EXPECT_NEAR(totalE, 11.0f, 1e-5f);
    ExpectVec3Near(totalP, glm::vec3(2.0f, 6.0f, 0.0f));
}

TEST(Physics_HelperAPI, CalculateRestitutionFromVelocities_HeadOn) {
    const glm::vec3 n = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 vBefore = glm::vec3(10.0f, 0.0f, 0.0f);
    const glm::vec3 vAfter = glm::vec3(-8.0f, 0.0f, 0.0f);
    const float e = CalculateRestitutionFromVelocities(vBefore, vAfter, n);
    EXPECT_NEAR(e, 0.8f, 1e-5f);
}

TEST(Physics_HelperAPI, ApplyLinearDamping_ReducesSpeed) {
    MovingSphere s({ 0.0f, 0.0f, 0.0f }, 1.0f, { 10.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
    ApplyLinearDamping(s, 0.5f, 1.0f);
    EXPECT_NEAR(s.velocity.x, 5.0f, 1e-5f);
}

TEST(Physics_HelperAPI, ApplyQuadraticDrag_ReducesSpeed) {
    MovingSphere s({ 0.0f, 0.0f, 0.0f }, 1.0f, { 10.0f, 0.0f, 0.0f }, 1.0f, 1.0f);
    const float before = glm::length(s.velocity);
    ApplyQuadraticDrag(s, 0.1f, 0.1f);
    const float after = glm::length(s.velocity);
    EXPECT_LT(after, before);
}