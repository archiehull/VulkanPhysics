#include "pch.h"
#include "Sphere.h"
#include "PhysicsHelper.h"

// -----------------------------------------------------------------------------
// Physics: Energy Conservation Tests
// -----------------------------------------------------------------------------

TEST(Physics_Energy, Conservation_PerfectlyElastic) {
    // Setup: Two balls colliding with generic non-axis-aligned velocities
    // Ball A: Moving diagonally right-up
    // Ball B: Moving left, about to hit A
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 10.0, 5.0, 0.0 });
    MovingSphere b({ 2.0, 1.0, 0.0 }, 1.0f, { -5.0, -2.0, 0.0 }); // Positioned to hit

    // 1. Measure Initial Energy
    double ke_initial_A = GetKineticEnergy(a);
    double ke_initial_B = GetKineticEnergy(b);
    double total_ke_initial = ke_initial_A + ke_initial_B;

    // 2. Resolve Collision
    ResolveElasticCollision(a, b);

    // 3. Measure Final Energy
    double ke_final_A = GetKineticEnergy(a);
    double ke_final_B = GetKineticEnergy(b);
    double total_ke_final = ke_final_A + ke_final_B;

    // 4. Assert Equality
    // We use a small epsilon (1e-6) to account for floating-point errors
    EXPECT_NEAR(total_ke_initial, total_ke_final, 1e-6)
        << "Kinetic Energy was lost or gained! \n"
        << "Initial: " << total_ke_initial << "\n"
        << "Final:   " << total_ke_final;
}

// -----------------------------------------------------------------------------
// Physics: Conservation of Momentum (m1u1 + m2u2 = m1v1 + m2v2)
// -----------------------------------------------------------------------------

TEST(Physics_Momentum, Conservation_EqualMass_HeadOn) {
    // DATA:
    // Ball A: Mass 1.0, Moving Right (10,0,0) at Origin
    // Ball B: Mass 1.0, Stationary at (2,0,0)
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 10.0, 0.0, 0.0 }, 1.0);
    MovingSphere b({ 2.0, 0.0, 0.0 }, 1.0f, { 0.0, 0.0, 0.0 }, 1.0);

    // 1. Calculate Initial Momentum
    Vec3 p1_initial = GetMomentum(a); // (10, 0, 0)
    Vec3 p2_initial = GetMomentum(b); // (0, 0, 0)
    Vec3 p_total_initial = p1_initial + p2_initial;

    // 2. Resolve Collision
    ResolveElasticCollision(a, b);

    // 3. Calculate Final Momentum
    Vec3 p1_final = GetMomentum(a);
    Vec3 p2_final = GetMomentum(b);
    Vec3 p_total_final = p1_final + p2_final;

    // 4. Assert Conservation
    ExpectVec3Near(p_total_initial, p_total_final);
}

TEST(Physics_Momentum, Conservation_UnequalMass_HeadOn) {
    // DATA:
    // Ball A (Heavy): Mass 10.0, Moving Right (5,0,0)
    // Ball B (Light): Mass 1.0, Moving Left (-5,0,0)
    // This tests if the mass weighting in the impulse formula is correct.
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 5.0, 0.0, 0.0 }, 10.0);
    MovingSphere b({ 2.0, 0.0, 0.0 }, 1.0f, { -5.0, 0.0, 0.0 }, 1.0);

    Vec3 p_total_initial = GetMomentum(a) + GetMomentum(b);

    ResolveElasticCollision(a, b);

    Vec3 p_total_final = GetMomentum(a) + GetMomentum(b);

    ExpectVec3Near(p_total_initial, p_total_final);
}

TEST(Physics_Momentum, Conservation_UnequalMass_Diagonal) {
    // DATA:
    // Ball A: Mass 2.0, Moving Diagonally (3, 3, 0)
    // Ball B: Mass 3.0, Stationary at collision point relative to A
    // Positions set to ensure center-to-center vector is aligned with movement
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 3.0, 3.0, 0.0 }, 2.0);
    MovingSphere b({ 2.0, 2.0, 0.0 }, 1.0f, { 0.0, 0.0, 0.0 }, 3.0);

    Vec3 p_total_initial = GetMomentum(a) + GetMomentum(b);

    ResolveElasticCollision(a, b);

    Vec3 p_total_final = GetMomentum(a) + GetMomentum(b);

    ExpectVec3Near(p_total_initial, p_total_final);
}

TEST(Physics_Momentum, Conservation_GlancingBlow) {
    // DATA:
    // A glancing collision where the normal is NOT aligned with the velocity.
    // Ball A: at (0,0,0) moving Right (10,0,0)
    // Ball B: at (1.5, 0.5, 0) -- offset in Y, so they hit at an angle.
    // Radii are 1.0, so distance (sqrt(1.5^2 + 0.5^2) = 1.58) < 2.0, they collide.
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 10.0, 0.0, 0.0 }, 1.0);
    MovingSphere b({ 1.5, 0.5, 0.0 }, 1.0f, { 0.0, 0.0, 0.0 }, 1.0);

    Vec3 p_total_initial = GetMomentum(a) + GetMomentum(b);

    ResolveElasticCollision(a, b);

    Vec3 p_total_final = GetMomentum(a) + GetMomentum(b);

    ExpectVec3Near(p_total_initial, p_total_final);
}