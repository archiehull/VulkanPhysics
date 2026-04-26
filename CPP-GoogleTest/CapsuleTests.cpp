#include "pch.h"
#include <Capsule.h>
#include <Plane.h>
#include <Sphere.h>

TEST(CapsuleTests, IsInside) {
	Capsule cap(glm::vec3(0, 0, 0), glm::vec3(0, 4, 0), 2.0f);
	
	// Inside body
	EXPECT_TRUE(cap.IsInside(glm::vec3(0, 2, 0)));
	EXPECT_TRUE(cap.IsInside(glm::vec3(1, 2, 0)));
	EXPECT_TRUE(cap.IsInside(glm::vec3(0, 2, 1)));
	
	// Inside caps
	EXPECT_TRUE(cap.IsInside(glm::vec3(0, 5, 0))); // Top cap
	EXPECT_TRUE(cap.IsInside(glm::vec3(0, -1, 0))); // Bottom cap
	
	// Outside
	EXPECT_FALSE(cap.IsInside(glm::vec3(0, 7, 0)));
	EXPECT_FALSE(cap.IsInside(glm::vec3(3, 2, 0)));
}

TEST(CapsuleTests, IntersectsPlane) {
	Capsule cap(glm::vec3(0, 2, 0), glm::vec3(0, 6, 0), 1.0f);
	
	// Plane far below
	Plane p1(glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), 0.0f);
	EXPECT_FALSE(cap.Intersects(p1));
	
	// Plane touching bottom cap
	Plane p2(glm::vec3(0, 1, 0), glm::vec3(0, 1, 0), 0.0f);
	EXPECT_TRUE(cap.Intersects(p2));
	
	// Plane intersecting body
	Plane p3(glm::vec3(0, 4, 0), glm::vec3(0, 1, 0), 0.0f);
	EXPECT_TRUE(cap.Intersects(p3));
	
	// Plane touching top cap
	Plane p4(glm::vec3(0, 7, 0), glm::vec3(0, -1, 0), 0.0f);
	EXPECT_TRUE(cap.Intersects(p4));
}

TEST(CapsuleTests, IntersectsSphere) {
	Capsule cap(glm::vec3(0, 0, 0), glm::vec3(0, 4, 0), 1.0f);
	
	// Sphere outside
	Sphere s1(glm::vec3(0, 6, 0), 0.5f);
	EXPECT_FALSE(cap.Intersects(s1));
	
	// Sphere touching top cap
	Sphere s2(glm::vec3(0, 5.5f, 0), 0.5f);
	EXPECT_TRUE(cap.Intersects(s2));
	
	// Sphere touching side
	Sphere s3(glm::vec3(2.0f, 2.0f, 0), 0.5f); // dist to segment = 2.0, r1+r2 = 1.5 -> FALSE
	EXPECT_FALSE(cap.Intersects(s3));
	
	Sphere s4(glm::vec3(1.4f, 2.0f, 0), 0.5f); // dist to segment = 1.4, r1+r2 = 1.5 -> TRUE
	EXPECT_TRUE(cap.Intersects(s4));
}
