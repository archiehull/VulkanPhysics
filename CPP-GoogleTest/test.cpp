#include "pch.h"
#include "Sphere.h"
#include "Plane.h"
#include "Collider.h"
#include <memory>

/*
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

// Sphere intersection tests
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

// Plane tests
TEST(IsInside_Plane, HalfSpaceAbove) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }); // plane y = 0, inside is y >= 0
    Vec3 above{ 0.0, 1.0, 0.0 };
    Vec3 below{ 0.0, -1.0, 0.0 };
    Vec3 onPlane{ 0.0, 0.0, 0.0 };
    EXPECT_TRUE(p.IsInside(above));
    EXPECT_FALSE(p.IsInside(below));
    EXPECT_TRUE(p.IsInside(onPlane)); // point on plane considered inside
}

TEST(Intersects_Plane, SegmentCrossesPlane) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Line seg{ { 0.0, -1.0, 0.0 }, { 0.0, 1.0, 0.0 } };
    EXPECT_TRUE(p.Intersects(seg));
}

TEST(Intersects_Plane, SegmentParallelNoIntersection) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Line seg{ { 0.0, 1.0, 0.0 }, { 1.0, 1.0, 0.0 } }; // both endpoints above plane
    EXPECT_FALSE(p.Intersects(seg));
}

// Polymorphism via Collider
TEST(Collider_Polymorphic, SphereAsCollider) {
    std::unique_ptr<Collider> c(new Sphere({ 0.0, 0.0, 0.0 }, 5.0f));
    Vec3 inside{ 3.0, 4.0, 0.0 };
    Vec3 outside{ 6.0, 0.0, 0.0 };
    EXPECT_TRUE(c->IsInside(inside));
    EXPECT_FALSE(c->IsInside(outside));

    Line seg{ { -10.0, 0.0, 0.0 }, { 10.0, 0.0, 0.0 } };
    EXPECT_TRUE(c->Intersects(seg));
}

TEST(Collider_Polymorphic, PlaneAsCollider) {
    std::unique_ptr<Collider> c(new Plane({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }));
    Vec3 above{ 0.0, 2.0, 0.0 };
    Vec3 below{ 0.0, -2.0, 0.0 };
    EXPECT_TRUE(c->IsInside(above));
    EXPECT_FALSE(c->IsInside(below));

    Line seg{ { 0.0, -1.0, 0.0 }, { 0.0, 1.0, 0.0 } };
    EXPECT_TRUE(c->Intersects(seg));
}

// Sphere-sphere collision tests
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
    Sphere a({ 6.0 *multi, 6.0 * multi, 6.0 * multi }, 5.0f * multi);
    Sphere b({ 7.0 * multi, 6.0 * multi, 6.0 * multi }, 2.0f * multi);
    EXPECT_TRUE(a.CollideWith(b));
}

TEST(SphereSphereCollision, IdenticalSpheres) {
    Sphere a({ 0.0, 0.0, 0.0 }, 2.0f);
    Sphere b({ 0.0, 0.0, 0.0 }, 2.0f);
    EXPECT_TRUE(a.CollideWith(b));
}


// infinite line distance tests
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

TEST(Intersects_InfiniteLine_NoEps, NoIntersection_CentreAtOrigin) {
    Sphere s({ 0.0, 0.0, 0.0 }, 3.0f);
    InfiniteLine line{ {5.0, 5.0, 5.0}, {1.0, 0.0, 0.0} };
    double dist = Sphere::ShortestDistanceToLine(line, s.Position());
    // Expect distance strictly greater than radius (no EPS)
    EXPECT_GT(dist, static_cast<double>(s.m_radius));
}

TEST(Intersects_InfiniteLine_NoEps, PassesThroughSphere_CentreAtSphere) {
    Sphere s({ 10.0, 0.0, 0.0 }, 5.0f);
    InfiniteLine line{ {10.0, 0.0, 0.0}, {-1.0, 0.0, 0.0} }; // line passes through sphere center
    double dist = Sphere::ShortestDistanceToLine(line, s.Position());
    EXPECT_LE(dist, static_cast<double>(s.m_radius));
}

TEST(Intersects_InfiniteLine_NoEps, LineStartsInsideSphere) {
    Sphere s({ 2.0, 2.0, 2.0 }, 5.0f);
    InfiniteLine line{ {3.0, 2.0, 2.0}, {1.0, 0.0, 0.0} }; // start point lies within sphere
    double dist = Sphere::ShortestDistanceToLine(line, s.Position());
    EXPECT_LE(dist, static_cast<double>(s.m_radius));
}

TEST(Intersects_InfiniteLine_NoEps, LinePassesThroughSphereCenter) {
    Sphere s({ 0.0, 0.0, 0.0 }, 3.0f);
    InfiniteLine line{ {-5.0, 0.0, 0.0}, {1.0, 0.0, 0.0} }; // line goes through sphere center
    double dist = Sphere::ShortestDistanceToLine(line, s.Position());
    EXPECT_LE(dist, static_cast<double>(s.m_radius));
}

// Plane distance tests
TEST(PlaneDistance, PointAbovePlane) {
    // Plane through origin with normal Up (0,0,1)
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 });
    Vec3 point{ 2.0, 3.0, 5.0 }; // 5 units above
    EXPECT_NEAR(p.DistanceFromPoint(point), 5.0, 0.01);
}

TEST(PlaneDistance, PointBelowPlane) {
    // Plane through origin with normal Up (0,0,1)
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 });
    Vec3 point{ 2.0, 3.0, -4.0 }; // 4 units below
    EXPECT_NEAR(p.DistanceFromPoint(point), 4.0, 0.01);
}

TEST(PlaneDistance, PointOnPlane) {
    // Plane through (1,1,1) with normal (1,1,1)
    // The point (0,2,1) lies on this plane? 
    // Plane eq: 1(x-1) + 1(y-1) + 1(z-1) = 0 => x+y+z - 3 = 0
    // Check (0,2,1): 0 + 2 + 1 - 3 = 0. Yes, it's on the plane.
    Plane p({ 1.0, 1.0, 1.0 }, { 1.0, 1.0, 1.0 });
    Vec3 point{ 0.0, 2.0, 1.0 };
    EXPECT_NEAR(p.DistanceFromPoint(point), 0.0, 0.01);
}

TEST(PlaneDistance, PointCloseToPlane) {
    // Plane through origin, normal (1,1,0) -> normalised is (0.707, 0.707, 0)
    Plane p({ 0.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 });
    Vec3 point{ 1.0, 1.0, 1.0 };
    // Distance should be sqrt(2) approx 1.41
    EXPECT_NEAR(p.DistanceFromPoint(point), 1.4142, 0.01);
}

TEST(PlaneDistance, NegativeCoordinates) {
    // Plane through (-2,-2,-2), normal (1,1,1)
    Plane p({ -2.0, -2.0, -2.0 }, { 1.0, 1.0, 1.0 });
    Vec3 point{ -1.0, -1.0, -1.0 };
    // Distance should be sqrt(3) approx 1.73
    EXPECT_NEAR(p.DistanceFromPoint(point), 1.732, 0.01);
}

TEST(PlaneDistance, AlongNormalDirection) {
    // Plane through origin, normal (1,1,0)
    Plane p({ 0.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 });
    Vec3 point{ 1.0, 1.0, 0.0 };
    // Point is exactly along the normal vector direction from origin
    // Distance is length of (1,1,0) which is sqrt(2) ~ 1.41
    EXPECT_NEAR(p.DistanceFromPoint(point), 1.4142, 0.01);
}

TEST(PlaneDistance, RandomDirection) {
    // Plane through origin, normal (1, -1, 0)
    Plane p({ 0.0, 0.0, 0.0 }, { 1.0, -1.0, 0.0 });
    Vec3 point{ 1.0, 2.0, 3.0 };
    // Normal normalized: (0.707, -0.707, 0)
    // Dot((1,2,3), n) = 1*0.707 + 2*-0.707 + 0 = -0.707
    // Distance is abs(-0.707) = 0.707
    EXPECT_NEAR(p.DistanceFromPoint(point), 0.7071, 0.01);
}

// Sphere-plane collision tests
TEST(SpherePlaneCollision, NoIntersection_Above) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }); // Plane y=0
    Sphere s({ 0.0, 5.0, 0.0 }, 1.0f);             // Sphere at y=5, radius=1

    // Distance (5) > Radius (1) -> No Collision
    EXPECT_FALSE(p.Intersects(s));
}

TEST(SpherePlaneCollision, Intersection_Crossing) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Sphere s({ 0.0, 0.5, 0.0 }, 1.0f); // Sphere center at y=0.5

    // Distance (0.5) < Radius (1) -> Collision
    EXPECT_TRUE(p.Intersects(s));
}

TEST(SpherePlaneCollision, Intersection_Touching) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Sphere s({ 0.0, 1.0, 0.0 }, 1.0f); // Sphere bottom touches plane

    // Distance (1.0) == Radius (1.0) -> Collision
    EXPECT_TRUE(p.Intersects(s));
}

TEST(SpherePlaneCollision, Intersection_Bisected) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Sphere s({ 0.0, 0.0, 0.0 }, 1.0f); // Sphere center ON the plane

    // Distance (0) < Radius (1) -> Collision
    EXPECT_TRUE(p.Intersects(s));
}

TEST(SpherePlaneCollision, NoIntersection_Below) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Sphere s({ 0.0, -5.0, 0.0 }, 1.0f); // Sphere at y=-5

    // Distance (|-5|) = 5 > Radius (1) -> No Collision
    EXPECT_FALSE(p.Intersects(s));
}

*/