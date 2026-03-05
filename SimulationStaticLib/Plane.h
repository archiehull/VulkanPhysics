#pragma once
#include "Collider.h"
#include "Sphere.h"

class Plane : public Collider
{
public:
	Plane(const glm::vec3& pointOnPlane, const glm::vec3& normal);
	Plane(const glm::vec3& pointOnPlane, const glm::vec3& normal, float size);

	bool IsInside(const glm::vec3& point) const override;
	bool Intersects(const Line& line) const override;
	bool Intersects(const Sphere& sphere) const;

	float DistanceFromPoint(const glm::vec3& point) const;
	glm::vec3 GetNormal() const;
	float GetSignedDistance(const glm::vec3& p) const;

private:
	glm::vec3 m_normal;
	float m_d{ 0.0f };
	float m_size{ 0.0f };
};