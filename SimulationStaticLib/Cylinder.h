#pragma once
#include "Collider.h"
#include "Sphere.h" 
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp> // Required for length2 (LengthSq)
#include <algorithm>

class Cylinder : public Collider
{
public:
	// Cylinder defined by a segment (p1 to p2) and a radius
	Cylinder(const glm::vec3& p1, const glm::vec3& p2, float radius)
		: Collider(p1), m_p2(p2), m_radius(radius)
	{
	}

	// Point-inside-cylinder test
	bool IsInside(const glm::vec3& point) const override
	{
		glm::vec3 axis = m_p2 - m_position;
		glm::vec3 toPoint = point - m_position;

		float axisLengthSq = glm::length2(axis);

		if (axisLengthSq <= 1e-6f)
			return glm::length2(toPoint) <= (m_radius * m_radius);

		float t = glm::dot(toPoint, axis) / axisLengthSq;

		if (t < 0.0f || t > 1.0f) return false;

		glm::vec3 closestOnAxis = m_position + (axis * t);
		float distSq = glm::length2(point - closestOnAxis);
		float rSq = m_radius * m_radius;

		return distSq <= rSq + 1e-6f;
	}

	// Sphere-Cylinder Intersection
	bool Intersects(const Sphere& sphere) const
	{
		// Find closest point on the cylinder axis to the sphere center
		glm::vec3 closest = ClosestPointOnSegment(m_position, m_p2, sphere.Position());

		// Calculate distance squared between sphere center and that point
		float distSq = glm::length2(sphere.Position() - closest);

		// Check against sum of radii squared
		float rSum = m_radius + sphere.m_radius;
		return distSq <= (rSum * rSum) + 1e-6f;
	}

	// Placeholder for line intersection (required by base class)
	bool Intersects(const Line& line) const override { return false; }

public:
	glm::vec3 m_p2;
	float m_radius;

private:
	// Helper: Closest point on line segment ab to point p
	static glm::vec3 ClosestPointOnSegment(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p)
	{
		glm::vec3 ab = b - a;
		float abLenSq = glm::length2(ab);

		if (abLenSq <= 1e-6f) return a;

		float t = glm::dot(p - a, ab) / abLenSq;

		// Clamp t to segment [0, 1]
		t = glm::clamp(t, 0.0f, 1.0f);

		return a + (ab * t);
	}
};