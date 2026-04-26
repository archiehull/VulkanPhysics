#include "pch.h"
#include "Capsule.h"
#include <algorithm>

Capsule::Capsule(const glm::vec3& p1, const glm::vec3& p2, float radius)
	: Collider(p1), m_p2(p2), m_radius(radius)
{
}

bool Capsule::IsInside(const glm::vec3& point) const
{
	glm::vec3 closest = ClosestPointOnSegment(point);
	glm::vec3 diff = point - closest;
	return glm::dot(diff, diff) <= (m_radius * m_radius);
}

bool Capsule::Intersects(const Sphere& sphere) const
{
	glm::vec3 closest = ClosestPointOnSegment(sphere.Position());
	glm::vec3 diff = sphere.Position() - closest;
	float rSum = m_radius + sphere.m_radius;
	return glm::dot(diff, diff) <= (rSum * rSum);
}

bool Capsule::Intersects(const Plane& plane) const
{
	float d1 = plane.GetSignedDistance(m_position);
	float d2 = plane.GetSignedDistance(m_p2);
	
	// If either endpoint is within radius of the plane, or they are on opposite sides
	if (std::abs(d1) <= m_radius || std::abs(d2) <= m_radius) return true;
	if ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) return true;
	
	return false;
}

bool Capsule::Intersects(const Line& line) const 
{ 
	// Not implemented for now, as it's not strictly needed for the task
	return false; 
}

glm::vec3 Capsule::ClosestPointOnSegment(const glm::vec3& p) const
{
	glm::vec3 ab = m_p2 - m_position;
	float t = glm::dot(p - m_position, ab) / glm::dot(ab, ab);
	t = glm::clamp(t, 0.0f, 1.0f);
	return m_position + t * ab;
}
