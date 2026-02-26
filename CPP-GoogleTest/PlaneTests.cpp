#include "pch.h"
#include "Plane.h"
#include "Sphere.h" 
#include <glm/glm.hpp>

// -----------------------------------------------------------------------------
// Plane Point Containment
// -----------------------------------------------------------------------------
TEST(IsInside_Plane, HalfSpaceAbove) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }); // plane y = 0, inside is y >= 0
    glm::vec3 above{ 0.0, 1.0, 0.0 };
    glm::vec3 below{ 0.0, -1.0, 0.0 };
    glm::vec3 onPlane{ 0.0, 0.0, 0.0 };
    EXPECT_TRUE(p.IsInside(above));
    EXPECT_FALSE(p.IsInside(below));
    EXPECT_TRUE(p.IsInside(onPlane)); // point on plane considered inside
}

// -----------------------------------------------------------------------------
// Plane Line Segment Intersection
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Plane Distance Tests (Q4)
// -----------------------------------------------------------------------------
TEST(PlaneDistance, PointAbovePlane) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 });
    glm::vec3 point{ 2.0, 3.0, 5.0 }; // 5 units above
    EXPECT_NEAR(p.DistanceFromPoint(point), 5.0, 0.01);
}

TEST(PlaneDistance, PointBelowPlane) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 });
    glm::vec3 point{ 2.0, 3.0, -4.0 }; // 4 units below
    EXPECT_NEAR(p.DistanceFromPoint(point), 4.0, 0.01);
}

TEST(PlaneDistance, PointOnPlane) {
    Plane p({ 1.0, 1.0, 1.0 }, { 1.0, 1.0, 1.0 });
    glm::vec3 point{ 0.0, 2.0, 1.0 };
    EXPECT_NEAR(p.DistanceFromPoint(point), 0.0, 0.01);
}

TEST(PlaneDistance, PointCloseToPlane) {
    Plane p({ 0.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 });
    glm::vec3 point{ 1.0, 1.0, 1.0 };
    EXPECT_NEAR(p.DistanceFromPoint(point), 1.4142, 0.01);
}

TEST(PlaneDistance, NegativeCoordinates) {
    Plane p({ -2.0, -2.0, -2.0 }, { 1.0, 1.0, 1.0 });
    glm::vec3 point{ -1.0, -1.0, -1.0 };
    EXPECT_NEAR(p.DistanceFromPoint(point), 1.732, 0.01);
}

TEST(PlaneDistance, AlongNormalDirection) {
    Plane p({ 0.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 });
    glm::vec3 point{ 1.0, 1.0, 0.0 };
    EXPECT_NEAR(p.DistanceFromPoint(point), 1.4142, 0.01);
}

TEST(PlaneDistance, RandomDirection) {
    Plane p({ 0.0, 0.0, 0.0 }, { 1.0, -1.0, 0.0 });
    glm::vec3 point{ 1.0, 2.0, 3.0 };
    EXPECT_NEAR(p.DistanceFromPoint(point), 0.7071, 0.01);
}

// -----------------------------------------------------------------------------
// Sphere-Plane Collision (Q5)
// -----------------------------------------------------------------------------
TEST(SpherePlaneCollision, NoIntersection_Above) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }); // Plane y=0
    Sphere s({ 0.0, 5.0, 0.0 }, 1.0f);             // Sphere at y=5, radius=1
    EXPECT_FALSE(p.Intersects(s));
}

TEST(SpherePlaneCollision, Intersection_Crossing) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Sphere s({ 0.0, 0.5, 0.0 }, 1.0f); // Sphere center at y=0.5
    EXPECT_TRUE(p.Intersects(s));
}

TEST(SpherePlaneCollision, Intersection_Touching) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Sphere s({ 0.0, 1.0, 0.0 }, 1.0f); // Sphere bottom touches plane
    EXPECT_TRUE(p.Intersects(s));
}

TEST(SpherePlaneCollision, Intersection_Bisected) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Sphere s({ 0.0, 0.0, 0.0 }, 1.0f); // Sphere center ON the plane
    EXPECT_TRUE(p.Intersects(s));
}

TEST(SpherePlaneCollision, NoIntersection_Below) {
    Plane p({ 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 });
    Sphere s({ 0.0, -5.0, 0.0 }, 1.0f); // Sphere at y=-5
    EXPECT_FALSE(p.Intersects(s));
}