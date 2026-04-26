#include "pch.h"
#include <AABB.h>

TEST(AABBTests, IsInside) {
	AABB box(glm::vec3(0, 0, 0), glm::vec3(2, 2, 2));
	
	EXPECT_TRUE(box.IsInside(glm::vec3(0, 0, 0)));
	EXPECT_TRUE(box.IsInside(glm::vec3(1, 1, 1)));
	EXPECT_TRUE(box.IsInside(glm::vec3(2, 2, 2)));
	
	EXPECT_FALSE(box.IsInside(glm::vec3(3, 0, 0)));
	EXPECT_FALSE(box.IsInside(glm::vec3(0, -3, 0)));
}

TEST(AABBTests, IntersectsAABB) {
	AABB box1(glm::vec3(0, 0, 0), glm::vec3(2, 2, 2));
	AABB box2(glm::vec3(3, 0, 0), glm::vec3(2, 2, 2)); // center 3, half 2 -> min 1, max 5. Overlaps 1..2
	
	EXPECT_TRUE(box1.Intersects(box2));
	
	AABB box3(glm::vec3(5, 0, 0), glm::vec3(1, 1, 1)); // min 4, max 6. box1 max is 2.
	EXPECT_FALSE(box1.Intersects(box3));
}
