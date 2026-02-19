#include "pch.h"
#include "Sphere.h"
#include "PhysicsHelper.h"

// -----------------------------------------------------------------------------
// Sphere Point Containment
// -----------------------------------------------------------------------------
TEST(IsInside, BasicCentreInside) {
    Sphere sphere({ 0.0, 0.0, 0.0 }, 5.0);
    Vec3 point{ 0.0, 0.0, 0.0 };
    EXPECT_TRUE(sphere.IsInside(point));
}

TEST(IsInside, DiagonalInside) {
    Sphere sphere({ 0.0, 0.0, 0.0 }, 5.0);
    Vec3 point{ 3.0, 4.0, 0.0 }; // distance 5
    EXPECT_TRUE(sphere.IsInside(point));
}

TEST(IsInside, NonOriginInside) {
    Sphere sphere({ 2.0, 3.0, -1.0 }, 10.0);
    Vec3 point{ 5.0, 6.0, -2.0 }; // ~4.36 from center
    EXPECT_TRUE(sphere.IsInside(point));
}

TEST(IsInside, PrecisionInside) {
    Sphere sphere({ 0.0, 0.0, 0.0 }, 5.0);
    Vec3 point{ 4.999999, 0.0, 0.0 }; // ~4.999999 < 5
    EXPECT_TRUE(sphere.IsInside(point));
}

TEST(IsInside, NegativeCoordsInside) {
    Sphere sphere({ -2.0, -3.0, -4.0 }, 7.0);
    Vec3 point{ -5.0, -6.0, -4.0 }; // ~4.2426 from center
    EXPECT_TRUE(sphere.IsInside(point));
}

TEST(IsInside, CloseCallInside) {
    Sphere sphere({ 7.0, 8.0, 9.0 }, 10.0);
    Vec3 point{ 16.99, 8.0, 9.0 }; // ~9.99 < 10
    EXPECT_TRUE(sphere.IsInside(point));
}

TEST(IsInside, BasicCentreOutside) {
    Sphere sphere({ 0.0, 0.0, 0.0 }, 5.0);
    Vec3 point{ 6.0, 0.0, 0.0 };
    EXPECT_FALSE(sphere.IsInside(point));
}

TEST(IsInside, DiagonalOutside) {
    Sphere sphere({ 0.0, 0.0, 0.0 }, 5.0);
    Vec3 point{ 4.0, 4.0, 0.0 }; // ~5.66
    EXPECT_FALSE(sphere.IsInside(point));
}

TEST(IsInside, NonOriginOutside) {
    Sphere sphere({ 2.0, 3.0, -1.0 }, 10.0);
    Vec3 point{ 15.0, 3.0, -1.0 }; // 13
    EXPECT_FALSE(sphere.IsInside(point));
}

TEST(IsInside, PrecisionOutside) {
    Sphere sphere({ 0.0, 0.0, 0.0 }, 5.0);
    Vec3 point{ 5.000001, 0.0, 0.0 };
    EXPECT_FALSE(sphere.IsInside(point));
}

TEST(IsInside, NegativeCoordsOutside) {
    Sphere sphere({ -2.0, -3.0, -4.0 }, 7.0);
    Vec3 point{ -10.0, -3.0, -4.0 }; // 8
    EXPECT_FALSE(sphere.IsInside(point));
}

TEST(IsInside, CloseCallOutside) {
    Sphere sphere({ 7.0, 8.0, 9.0 }, 10.0);
    Vec3 point{ 17.01, 8.0, 9.0 }; // ~10.01
    EXPECT_FALSE(sphere.IsInside(point));
}

// -----------------------------------------------------------------------------
// Sphere Line Segment Intersection
// -----------------------------------------------------------------------------
TEST(Intersects_Sphere, SegmentThroughSphere) {
    Sphere s({ 0.0, 0.0, 0.0 }, 5.0f);
    Line seg{ { -10.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 } };
    EXPECT_TRUE(s.Intersects(seg));
}

TEST(Intersects_Sphere, SegmentMissesSphere) {
    Sphere s({ 0.0, 0.0, 0.0 }, 5.0f);
    Line seg{ { -10.0, 6.0, 0.0 }, { -6.0, 6.0, 0.0 } }; // passes above the sphere
    EXPECT_FALSE(s.Intersects(seg));
}

TEST(Intersects_Sphere, DegeneratePointInside) {
    Sphere s({ 0.0, 0.0, 0.0 }, 5.0f);
    Line seg{ { 1.0, 1.0, 1.0 }, { 1.0, 1.0, 1.0 } }; // point inside
    EXPECT_TRUE(s.Intersects(seg));
}

TEST(Intersects_Sphere, DegeneratePointOutside) {
    Sphere s({ 0.0, 0.0, 0.0 }, 5.0f);
    Line seg{ { 6.0, 0.0, 0.0 }, { 6.0, 0.0, 0.0 } }; // point outside
    EXPECT_FALSE(s.Intersects(seg));
}

TEST(Intersects_Sphere, SegmentStartInside) {
    Sphere s({ 0.0, 0.0, 0.0 }, 5.0f);
    // Start point (0,0,0) is inside, End point (10,0,0) is outside
    Line seg{ { 0.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 } };
    EXPECT_TRUE(s.Intersects(seg));
}

TEST(Intersects_Sphere, SegmentEndInside) {
    Sphere s({ 0.0, 0.0, 0.0 }, 5.0f);
    // Start point (10,0,0) is outside, End point (0,0,0) is inside
    Line seg{ { 10.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } };
    EXPECT_TRUE(s.Intersects(seg));
}

TEST(Intersects_Sphere, LineHits_ButSegmentIsAfter) {
    Sphere s({ 0.0, 0.0, 0.0 }, 5.0f);
    Line seg{ { 6.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 } };
    EXPECT_FALSE(s.Intersects(seg));
}

TEST(Intersects_Sphere, LineHits_ButSegmentIsBefore) {
    Sphere s({ 0.0, 0.0, 0.0 }, 5.0f);
    Line seg{ { -10.0, 0.0, 0.0 }, { -6.0, 0.0, 0.0 } };
    EXPECT_FALSE(s.Intersects(seg));
}

// -----------------------------------------------------------------------------
// Sphere-Sphere Collision (Q1)
// -----------------------------------------------------------------------------
TEST(SphereSphereCollision, NoIntersection_CentreAtOrigin) {
    Sphere a({ 0.0, 0.0, 0.0 }, 1.0f);
    Sphere b({ 5.0, 0.0, 0.0 }, 1.0f);
    EXPECT_FALSE(a.CollideWith(b));
}

TEST(SphereSphereCollision, NoIntersection_OffsetCentre) {
    Sphere a({ 3.0, 3.0, 3.0 }, 2.0f);
    Sphere b({ 10.0, 10.0, 10.0 }, 2.0f);
    EXPECT_FALSE(a.CollideWith(b));
}

TEST(SphereSphereCollision, Overlapping_CentreAtOrigin) {
    Sphere a({ 0.0, 0.0, 0.0 }, 2.0f);
    Sphere b({ 2.0, 0.0, 0.0 }, 2.0f);
    EXPECT_TRUE(a.CollideWith(b));
}

TEST(SphereSphereCollision, Overlapping_OffsetCentre) {
    Sphere a({ 5.0, 5.0, 5.0 }, 3.0f);
    Sphere b({ 8.0, 5.0, 5.0 }, 3.0f);
    EXPECT_TRUE(a.CollideWith(b));
}

TEST(SphereSphereCollision, FullyContained_CentreAtOrigin) {
    Sphere a({ 0.0, 0.0, 0.0 }, 3.0f);
    Sphere b({ 1.0, 0.0, 0.0 }, 1.0f);
    EXPECT_TRUE(a.CollideWith(b));
    EXPECT_TRUE(b.CollideWith(a)); // symmetric
}

TEST(SphereSphereCollision, FullyContained_OffsetCentre) {
    float multi = 100000000000000.1;
    Sphere a({ 6.0 * multi, 6.0 * multi, 6.0 * multi }, 5.0f * multi);
    Sphere b({ 7.0 * multi, 6.0 * multi, 6.0 * multi }, 2.0f * multi);
    EXPECT_TRUE(a.CollideWith(b));
}

TEST(SphereSphereCollision, IdenticalSpheres) {
    Sphere a({ 0.0, 0.0, 0.0 }, 2.0f);
    Sphere b({ 0.0, 0.0, 0.0 }, 2.0f);
    EXPECT_TRUE(a.CollideWith(b));
}

// -----------------------------------------------------------------------------
// Infinite Line Distance (Q2)
// -----------------------------------------------------------------------------
TEST(InfiniteLineDistance, ClosestPointOnLine) {
    InfiniteLine line{ {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0} };
    Vec3 PG{ 2.0, 3.0, 4.0 };
    double distance = Sphere::ShortestDistanceToLine(line, PG);
    EXPECT_NEAR(distance, 1.41421356, 0.01);
}

TEST(InfiniteLineDistance, PointOnLine) {
    InfiniteLine line{ {0.0, 0.0, 0.0}, {1.0, 2.0, 3.0} };
    Vec3 PG{ 3.0, 6.0, 9.0 };
    double distance = Sphere::ShortestDistanceToLine(line, PG);
    EXPECT_NEAR(distance, 0.0, 1e-9);
}

TEST(InfiniteLineDistance, VerticalLine) {
    InfiniteLine line{ {2.0, 2.0, 0.0}, {0.0, 0.0, 1.0} };
    Vec3 PG{ 4.0, 5.0, 3.0 };
    double distance = Sphere::ShortestDistanceToLine(line, PG);
    EXPECT_NEAR(distance, 3.60555, 0.01);
}

TEST(InfiniteLineDistance, HorizontalLine) {
    InfiniteLine line{ {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0} };
    Vec3 PG{ 3.0, 4.0, 5.0 };
    double distance = Sphere::ShortestDistanceToLine(line, PG);
    EXPECT_NEAR(distance, 6.40312, 0.01);
}

TEST(InfiniteLineDistance, DiagonalLine) {
    InfiniteLine line{ {1.0, 1.0, 1.0}, {1.0, -1.0, 1.0} };
    Vec3 PG{ 2.0, 5.0, 3.0 };
    double distance = Sphere::ShortestDistanceToLine(line, PG);
    EXPECT_NEAR(distance, 4.54606, 0.01);
}

// -----------------------------------------------------------------------------
// Sphere - Infinite Line Intersection (Q3)
// -----------------------------------------------------------------------------
TEST(Intersects_InfiniteLine_NoEps, NoIntersection_CentreAtOrigin) {
    Sphere s({ 0.0, 0.0, 0.0 }, 3.0f);
    InfiniteLine line{ {5.0, 5.0, 5.0}, {1.0, 0.0, 0.0} };
    double dist = Sphere::ShortestDistanceToLine(line, s.Position());
    EXPECT_GT(dist, static_cast<double>(s.m_radius));
}

TEST(Intersects_InfiniteLine_NoEps, PassesThroughSphere_CentreAtSphere) {
    Sphere s({ 10.0, 0.0, 0.0 }, 5.0f);
    InfiniteLine line{ {10.0, 0.0, 0.0}, {-1.0, 0.0, 0.0} };
    double dist = Sphere::ShortestDistanceToLine(line, s.Position());
    EXPECT_LE(dist, static_cast<double>(s.m_radius));
}

TEST(Intersects_InfiniteLine_NoEps, LineStartsInsideSphere) {
    Sphere s({ 2.0, 2.0, 2.0 }, 5.0f);
    InfiniteLine line{ {3.0, 2.0, 2.0}, {1.0, 0.0, 0.0} };
    double dist = Sphere::ShortestDistanceToLine(line, s.Position());
    EXPECT_LE(dist, static_cast<double>(s.m_radius));
}

TEST(Intersects_InfiniteLine_NoEps, LinePassesThroughSphereCenter) {
    Sphere s({ 0.0, 0.0, 0.0 }, 3.0f);
    InfiniteLine line{ {-5.0, 0.0, 0.0}, {1.0, 0.0, 0.0} };
    double dist = Sphere::ShortestDistanceToLine(line, s.Position());
    EXPECT_LE(dist, static_cast<double>(s.m_radius));
}

// -----------------------------------------------------------------------------
// Physics: Direct Collision Tests
// -----------------------------------------------------------------------------

TEST(Physics_Collision, DirectCollision_AxisAligned) {
    // Setup: Ball A moving Right, Ball B stationary
    // Positions allow them to just touch or overlap slightly for the calculation
    // A at (0,0,0), B at (2,0,0), Radius 1. Collision Normal is (1,0,0)
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 10.0, 0.0, 0.0 }); // Moving 10 m/s X
    MovingSphere b({ 2.0, 0.0, 0.0 }, 1.0f, { 0.0, 0.0, 0.0 });  // Stationary

    // Perform resolution
    ResolveElasticCollision(a, b);

    // Expectation: A stops, B takes A's velocity
    // Check A Velocity
    EXPECT_NEAR(a.velocity.x, 0.0, 1e-6);
    EXPECT_NEAR(a.velocity.y, 0.0, 1e-6);
    EXPECT_NEAR(a.velocity.z, 0.0, 1e-6);

    // Check B Velocity
    EXPECT_NEAR(b.velocity.x, 10.0, 1e-6);
    EXPECT_NEAR(b.velocity.y, 0.0, 1e-6);
    EXPECT_NEAR(b.velocity.z, 0.0, 1e-6);
}

TEST(Physics_Collision, DirectCollision_GeneralVelocity) {
    // Setup: Ball A moving diagonally, Ball B stationary
    // Direction is (1, 1, 1). Normalized ~ (0.577, 0.577, 0.577)
    // Position B is exactly along that path relative to A

    Vec3 direction{ 1.0, 1.0, 1.0 };
    Vec3 posA{ 0.0, 0.0, 0.0 };
    Vec3 posB{ 2.0, 2.0, 2.0 }; // B is at (2,2,2)

    // A moves exactly towards B with velocity (5,5,5)
    MovingSphere a(posA, 1.0f, { 5.0, 5.0, 5.0 });
    MovingSphere b(posB, 1.0f, { 0.0, 0.0, 0.0 });

    ResolveElasticCollision(a, b);

    // Expectation: A stops, B takes A's velocity
    // Check A Velocity
    EXPECT_NEAR(a.velocity.x, 0.0, 1e-6);
    EXPECT_NEAR(a.velocity.y, 0.0, 1e-6);
    EXPECT_NEAR(a.velocity.z, 0.0, 1e-6);

    // Check B Velocity
    EXPECT_NEAR(b.velocity.x, 5.0, 1e-6);
    EXPECT_NEAR(b.velocity.y, 5.0, 1e-6);
    EXPECT_NEAR(b.velocity.z, 5.0, 1e-6);
}

// -----------------------------------------------------------------------------
// Physics: Two Moving Balls (Direct Collision)
// -----------------------------------------------------------------------------

// Case 1: Head-on collision (Opposing directions)
// A moves Right (10), B moves Left (-10). 
// They should bounce off and swap velocities.
TEST(Physics_Collision, TwoMoving_Opposing_AxisAligned) {
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 10.0, 0.0, 0.0 });
    MovingSphere b({ 2.0, 0.0, 0.0 }, 1.0f, { -10.0, 0.0, 0.0 });

    ResolveElasticCollision(a, b);

    // A should now move Left (-10)
    EXPECT_NEAR(a.velocity.x, -10.0, 1e-6);

    // B should now move Right (10)
    EXPECT_NEAR(b.velocity.x, 10.0, 1e-6);
}

// Case 2: One ball catches the other (Same direction)
// A moves Fast (20), B moves Slow (10). A is behind B.
// They should swap: A slows down to 10, B speeds up to 20.
TEST(Physics_Collision, TwoMoving_SameDir_AxisAligned) {
    MovingSphere a({ 0.0, 0.0, 0.0 }, 1.0f, { 20.0, 0.0, 0.0 }); // Fast, Behind
    MovingSphere b({ 2.0, 0.0, 0.0 }, 1.0f, { 10.0, 0.0, 0.0 }); // Slow, Ahead

    ResolveElasticCollision(a, b);

    // A takes B's old velocity
    EXPECT_NEAR(a.velocity.x, 10.0, 1e-6);

    // B takes A's old velocity
    EXPECT_NEAR(b.velocity.x, 20.0, 1e-6);
}

// Case 3: Head-on collision with General Velocities (Diagonal)
// A moves (10,10,10), B moves (-10,-10,-10) along the diagonal axis.
// Positions are set so the collision normal aligns with the velocity.
TEST(Physics_Collision, TwoMoving_Opposing_General) {
    Vec3 p1{ 0.0, 0.0, 0.0 };
    Vec3 p2{ 2.0, 2.0, 2.0 }; // Direction (1,1,1)

    MovingSphere a(p1, 1.0f, { 10.0, 10.0, 10.0 });
    MovingSphere b(p2, 1.0f, { -10.0, -10.0, -10.0 });

    ResolveElasticCollision(a, b);

    // Swap expected
    // A becomes (-10, -10, -10)
    EXPECT_NEAR(a.velocity.x, -10.0, 1e-6);
    EXPECT_NEAR(a.velocity.y, -10.0, 1e-6);
    EXPECT_NEAR(a.velocity.z, -10.0, 1e-6);

    // B becomes (10, 10, 10)
    EXPECT_NEAR(b.velocity.x, 10.0, 1e-6);
    EXPECT_NEAR(b.velocity.y, 10.0, 1e-6);
    EXPECT_NEAR(b.velocity.z, 10.0, 1e-6);
}
