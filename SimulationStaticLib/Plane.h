#pragma once
#include "Collider.h"
#include "Sphere.h"
#include <cmath>

class Plane : public Collider
{
public:
	// Construct plane from a point on the plane (position) and a normal.
	Plane(const Vec3& pointOnPlane, const Vec3& normal)
		: Collider(pointOnPlane), m_normal(normal)
	{
		Normalize(m_normal);
		// plane equation: m_normal . x + m_d = 0
		m_d = -Dot(m_normal, m_position);
	}

	// Consider "inside" as the half-space where (n . x + d) >= 0
	bool IsInside(const Vec3& point) const override
	{
		double signedDist = Dot(m_normal, point) + m_d;
		return signedDist >= 0.0;
	}

	// Segment-plane intersection: true if endpoints are on opposite sides or one is on plane.
	bool Intersects(const Line& line) const override
	{
		double da = Dot(m_normal, line.a) + m_d;
		double db = Dot(m_normal, line.b) + m_d;

		// if either endpoint is exactly on the plane, we call that an intersection
		if (da == 0.0 || db == 0.0) return true;

		// intersection occurs if signs differ
		return (da < 0.0 && db > 0.0) || (da > 0.0 && db < 0.0);
	}

	bool Intersects(const Sphere& sphere) const
	{
		double dist = DistanceFromPoint(sphere.Position());
		return dist <= static_cast<double>(sphere.m_radius) + 1e-9;
	}

	// Returns the shortest distance from a general point to the plane.
	double DistanceFromPoint(const Vec3& point) const
	{
		return std::abs(Dot(m_normal, point) + m_d);
	}

private:
	static void Normalize(Vec3& v)
	{
		double lenSq = LengthSq(v);
		if (lenSq <= 0.0) return;
		double invLen = 1.0 / std::sqrt(lenSq);
		v.x *= invLen;
		v.y *= invLen;
		v.z *= invLen;
	}

	Vec3 m_normal;
	double m_d{ 0.0 };
};