#pragma once
#include "Collider.h"
#include "Sphere.h"

class Plane : public Collider
{
public:
	Plane(const glm::vec3& pointOnPlane, const glm::vec3& normal)
		: Collider(pointOnPlane), m_normal(glm::normalize(normal))
	{
		m_d = -glm::dot(m_normal, m_position);
	}

	bool IsInside(const glm::vec3& point) const override
	{
		return (glm::dot(m_normal, point) + m_d) >= 0.0f;
	}

	bool Intersects(const Line& line) const override
	{
		float da = glm::dot(m_normal, line.a) + m_d;
		float db = glm::dot(m_normal, line.b) + m_d;
		return (da * db) <= 0.0f; // Simplified intersection check
	}

	bool Intersects(const Sphere& sphere) const
	{
		float dist = std::abs(glm::dot(m_normal, sphere.Position()) + m_d);
		return dist <= sphere.m_radius + 1e-6f;
	}

private:
	glm::vec3 m_normal;
	float m_d{ 0.0f };
};