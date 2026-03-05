#pragma once
#include "Collider.h"
#include <glm/glm.hpp>

class AABB : public Collider
{
public:
    AABB(const glm::vec3& center, const glm::vec3& halfExtents);

    bool IsInside(const glm::vec3& point) const override;
    bool Intersects(const Line& line) const override;
    bool Intersects(const AABB& other) const;

    glm::vec3 GetMin() const;
    glm::vec3 GetMax() const;

    glm::vec3 m_halfExtents;
};
