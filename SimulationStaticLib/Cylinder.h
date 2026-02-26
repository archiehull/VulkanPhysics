#pragma once
#include "Collider.h"
#include "Sphere.h"

class Cylinder : public Collider
{
public:
	Cylinder(const glm::vec3& p1, const glm::vec3& p2, float radius)
		: Collider(p1), m_p2(p2), m_radius(radius)
	{
	}

	bool IsInside(const glm::vec3& point) const override
	{
		glm::vec3 axis = m_p2 - m_position;
		glm::vec3 toPoint = point - m_position;

		float axisLengthSq = glm::dot(axis, axis);

		if (axisLengthSq <= 1e-6f)
			return glm::dot(toPoint, toPoint) <= (m_radius * m_radius);

		float t = glm::dot(toPoint, axis) / axisLengthSq;

		if (t < 0.0f || t > 1.0f) return false;

		glm::vec3 closestOnAxis = m_position + (axis * t);
		glm::vec3 d = point - closestOnAxis;

		return glm::dot(d, d) <= (m_radius * m_radius) + 1e-6f;
	}

	bool Intersects(const Sphere& sphere) const
	{
		glm::vec3 closest = ClosestPointOnSegment(m_position, m_p2, sphere.Position());
		glm::vec3 d = sphere.Position() - closest;

		float rSum = m_radius + sphere.m_radius;
		return glm::dot(d, d) <= (rSum * rSum) + 1e-6f;
	}

	bool Intersects(const Line& line) const override { return false; }

public:
	glm::vec3 m_p2;
	float m_radius;

private:
	static glm::vec3 ClosestPointOnSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p)
	{
		glm::vec3 ab = b - a;
		float t = glm::dot(p - a, ab) / glm::dot(ab, ab);
		t = glm::clamp(t, 0.0f, 1.0f);
		return a + t * ab;
	}
};