#pragma once
#include "Collider.h"
#include "Sphere.h"
#include "Plane.h"

class Capsule : public Collider
{
public:
	Capsule(const glm::vec3& p1, const glm::vec3& p2, float radius);

	bool IsInside(const glm::vec3& point) const override;
	bool Intersects(const Sphere& sphere) const;
	bool Intersects(const Plane& plane) const;
	bool Intersects(const Line& line) const override;

	glm::vec3 m_p2;
	float m_radius;

	// Helper to find the closest point on the capsule's inner segment to a point
	glm::vec3 ClosestPointOnSegment(const glm::vec3& p) const;
};
