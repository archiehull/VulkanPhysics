#pragma once
#include "Collider.h"
#include <glm/gtx/norm.hpp> 

class Sphere : public Collider
{
public:
	Sphere(const glm::vec3& center, float radius)
		: Collider(center), m_radius(radius) {
	}

	bool IsInside(const glm::vec3& point) const override
	{
		return glm::distance2(point, m_position) <= (m_radius * m_radius) + EPS;
	}

	bool Intersects(const Line& line) const override
	{
		glm::vec3 closest = ClosestPointOnSegment(line, m_position);
		return glm::distance2(m_position, closest) <= (m_radius * m_radius) + EPS;
	}

	bool CollideWith(const Sphere& other) const
	{
		float rSum = m_radius + other.m_radius;
		return glm::distance2(m_position, other.m_position) <= (rSum * rSum) + EPS;
	}

	float m_radius;

private:
	static constexpr float EPS = 1e-6f;

	static glm::vec3 ClosestPointOnSegment(const Line& seg, const glm::vec3& point)
	{
		glm::vec3 ab = seg.b - seg.a;
		float abLenSq = glm::length2(ab);
		if (abLenSq <= 0.0f) return seg.a;

		float t = glm::dot(point - seg.a, ab) / abLenSq;
		t = glm::clamp(t, 0.0f, 1.0f);
		return seg.a + (ab * t);
	}
};