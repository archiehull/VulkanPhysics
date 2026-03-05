#pragma once
#include "Collider.h"
#include "Sphere.h"

class Cylinder : public Collider
{
public:
	Cylinder(const glm::vec3& p1, const glm::vec3& p2, float radius);

	bool IsInside(const glm::vec3& point) const override;
	bool Intersects(const Sphere& sphere) const;
	bool Intersects(const Line& line) const override;

	glm::vec3 m_p2;
	float m_radius;

private:
	static glm::vec3 ClosestPointOnSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p);
};