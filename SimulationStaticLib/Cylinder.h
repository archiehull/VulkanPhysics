#pragma once
#include "Collider.h"
#include "Sphere.h" // Required to access Sphere properties
#include <cmath>
#include <algorithm>

class Cylinder : public Collider
{
public:
	// Cylinder defined by a segment (p1 to p2) and a radius
	Cylinder(const Vec3& p1, const Vec3& p2, float radius)
		: Collider(p1), m_p2(p2), m_radius(radius)
	{
	}

	// Point-inside-cylinder test
	bool IsInside(const Vec3& point) const override
	{
		Vec3 axis = m_p2 - m_position;
		Vec3 toPoint = point - m_position;

		double axisLengthSq = LengthSq(axis);

		if (axisLengthSq <= 1e-9)
			return LengthSq(toPoint) <= (static_cast<double>(m_radius) * m_radius);

		double t = Dot(toPoint, axis) / axisLengthSq;

		if (t < 0.0 || t > 1.0) return false;

		Vec3 closestOnAxis = m_position + (axis * t);
		double distSq = LengthSq(point - closestOnAxis);
		double rSq = static_cast<double>(m_radius) * m_radius;

		return distSq <= rSq + 1e-9;
	}

	// Sphere-Cylinder Intersection
	bool Intersects(const Sphere& sphere) const
	{
		// Find closest point on the cylinder axis to the sphere center
		Vec3 closest = ClosestPointOnSegment(m_position, m_p2, sphere.Position());

		// Calculate distance squared between sphere center and that point
		double distSq = LengthSq(sphere.Position() - closest);

		// Check against sum of radii squared
		double rSum = static_cast<double>(m_radius) + static_cast<double>(sphere.m_radius);
		return distSq <= (rSum * rSum) + 1e-9;
	}

	// Placeholder for line intersection (required by base class)
	bool Intersects(const Line& line) const override { return false; }

public:
	Vec3 m_p2;
	float m_radius;

private:
	// Helper: Closest point on line segment ab to point p
	static Vec3 ClosestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p)
	{
		Vec3 ab = b - a;
		double abLenSq = LengthSq(ab);

		if (abLenSq <= 1e-9) return a;

		double t = Dot(p - a, ab) / abLenSq;

		// Clamp t to segment [0, 1]
		if (t < 0.0) t = 0.0;
		if (t > 1.0) t = 1.0;

		return a + (ab * t);
	}
};