#pragma once
#include "Collider.h"
#include <glm/glm.hpp>

class Sphere : public Collider
{
public:
	Sphere(const glm::vec3& center, float radius)
		: Collider(center), m_radius(radius) {
	}

	bool IsInside(const glm::vec3& point) const override
	{
		glm::vec3 d = point - m_position;
		return glm::dot(d, d) <= (m_radius * m_radius) + EPS;
	}

	bool Intersects(const Line& line) const override
	{
		glm::vec3 closest = ClosestPointOnSegment(line, m_position);
		glm::vec3 d = m_position - closest;
		return glm::dot(d, d) <= (m_radius * m_radius) + EPS;
	}

	bool Intersects(const InfiniteLine& line) const
	{
		float dist = ShortestDistanceToLine(line, m_position);
		return dist <= m_radius + EPS;
	}

	bool CollideWith(const Sphere& other) const
	{
		float rSum = m_radius + other.m_radius;
		glm::vec3 d = m_position - other.m_position;
		return glm::dot(d, d) <= (rSum * rSum) + EPS;
	}

	static glm::vec3 ClosestPointOnInfiniteLine(const InfiniteLine& line, const glm::vec3& PG)
	{
		glm::vec3 PLPG = PG - line.point;
		float m = glm::dot(PLPG, line.direction) / glm::dot(line.direction, line.direction);
		return line.point + (line.direction * m);
	}

	static float ShortestDistanceToLine(const InfiniteLine& line, const glm::vec3& PG)
	{
		glm::vec3 PA = ClosestPointOnInfiniteLine(line, PG);
		return glm::length(PG - PA);
	}

	float m_radius;

private:
	static constexpr float EPS = 1e-6f;

	static glm::vec3 ClosestPointOnSegment(const Line& seg, const glm::vec3& p)
	{
		glm::vec3 ab = seg.b - seg.a;
		float denom = glm::dot(ab, ab);

		// Degenerate segment: treat as single point
		if (denom <= EPS) {
			return seg.a;
		}

		float t = glm::dot(p - seg.a, ab) / denom;
		t = glm::clamp(t, 0.0f, 1.0f);
		return seg.a + t * ab;
	}
};