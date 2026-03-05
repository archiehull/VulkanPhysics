#pragma once
#include "Collider.h"
#include <glm/glm.hpp>

class Sphere : public Collider
{
public:
	Sphere(const glm::vec3& center, float radius);

	bool IsInside(const glm::vec3& point) const override;
	bool Intersects(const Line& line) const override;
	bool Intersects(const InfiniteLine& line) const;
	bool CollideWith(const Sphere& other) const;

	static glm::vec3 ClosestPointOnInfiniteLine(const InfiniteLine& line, const glm::vec3& PG);
	static float ShortestDistanceToLine(const InfiniteLine& line, const glm::vec3& PG);

	float m_radius;

private:
	static constexpr float EPS = 1e-6f;
	static glm::vec3 ClosestPointOnSegment(const Line& seg, const glm::vec3& p);
};