#include "pch.h"
#include "Collider.h"
#include "Sphere.h"
#include "Plane.h"
#include <memory>

// -----------------------------------------------------------------------------
// Polymorphism via Collider
// -----------------------------------------------------------------------------
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