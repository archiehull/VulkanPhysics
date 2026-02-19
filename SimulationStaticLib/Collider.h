#pragma once
#include <glm/glm.hpp>

struct InfiniteLine
{
	glm::vec3 point;
	glm::vec3 direction;
};

struct Line
{
	glm::vec3 a;
	glm::vec3 b; // line segment from a to b
};

class Collider
{
public:
	explicit Collider(const glm::vec3& position) : m_position(position) {}
	virtual ~Collider() = default;

	const glm::vec3& Position() const { return m_position; }
	void SetPosition(const glm::vec3& p) { m_position = p; }

	virtual bool IsInside(const glm::vec3& point) const = 0;
	virtual bool Intersects(const Line& line) const = 0;

protected:
	glm::vec3 m_position;
};