#pragma once
#include "Collider.h"
#include "Sphere.h"

class Plane : public Collider
{
public:
	Plane(const glm::vec3& pointOnPlane, const glm::vec3& normal)
		: Plane(pointOnPlane, normal, 0.0f)
	{
	}

	Plane(const glm::vec3& pointOnPlane, const glm::vec3& normal, float size)
		: Collider(pointOnPlane), m_normal(glm::normalize(normal)), m_size(size)
	{
		m_d = -glm::dot(m_normal, m_position);
	}

	bool IsInside(const glm::vec3& point) const override
	{
		return (glm::dot(m_normal, point) + m_d) >= 0.0f;
	}

	bool Intersects(const Line& line) const override
	{
		constexpr float EPS = 1e-6f;
		float da = glm::dot(m_normal, line.a) + m_d;
		float db = glm::dot(m_normal, line.b) + m_d;
		if (std::abs(da) <= EPS || std::abs(db) <= EPS) return true;
		return (da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f);
	}

	bool Intersects(const Sphere& sphere) const
	{
		constexpr float EPS = 1e-6f;
		float dist = DistanceFromPoint(sphere.Position());

		if (dist > sphere.m_radius + EPS) {
			return false;
		}

		// Optional finite plane extent check when m_size > 0
		if (m_size > 0.0f) {
			glm::vec3 toSphere = sphere.Position() - m_position;
			glm::vec3 pointOnPlane = toSphere - (m_normal * glm::dot(toSphere, m_normal));
			if (glm::length(pointOnPlane) > m_size + sphere.m_radius + EPS) {
				return false;
			}
		}

		return true;
	}

	float DistanceFromPoint(const glm::vec3& point) const
	{
		return std::abs(glm::dot(m_normal, point) + m_d);
	}

	glm::vec3 GetNormal() const { return m_normal; }
	float GetSignedDistance(const glm::vec3& p) const { return glm::dot(m_normal, p) + m_d; }

private:
	glm::vec3 m_normal;
	float m_d{ 0.0f };
	float m_size{ 0.0f };
};