#include "pch.h"
#include "AABB.h"

AABB::AABB(const glm::vec3& center, const glm::vec3& halfExtents)
    : Collider(center), m_halfExtents(halfExtents)
{
}

bool AABB::IsInside(const glm::vec3& point) const
{
    glm::vec3 min = m_position - m_halfExtents;
    glm::vec3 max = m_position + m_halfExtents;

    return (point.x >= min.x && point.x <= max.x &&
            point.y >= min.y && point.y <= max.y &&
            point.z >= min.z && point.z <= max.z);
}

bool AABB::Intersects(const Line& line) const
{
    glm::vec3 min = GetMin();
    glm::vec3 max = GetMax();
    glm::vec3 dir = line.b - line.a;
    
    float tmin = 0.0f;
    float tmax = 1.0f;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(dir[i]) < 1e-6f) {
            if (line.a[i] < min[i] || line.a[i] > max[i]) {
                return false;
            }
        } else {
            float t1 = (min[i] - line.a[i]) / dir[i];
            float t2 = (max[i] - line.a[i]) / dir[i];
            
            if (t1 > t2) std::swap(t1, t2);
            
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            
            if (tmin > tmax) return false;
        }
    }
    
    return true;
}

bool AABB::Intersects(const AABB& other) const
{
    glm::vec3 min1 = GetMin();
    glm::vec3 max1 = GetMax();
    glm::vec3 min2 = other.GetMin();
    glm::vec3 max2 = other.GetMax();

    return (min1.x <= max2.x && max1.x >= min2.x) &&
           (min1.y <= max2.y && max1.y >= min2.y) &&
           (min1.z <= max2.z && max1.z >= min2.z);
}

glm::vec3 AABB::GetMin() const
{
    return m_position - m_halfExtents;
}

glm::vec3 AABB::GetMax() const
{
    return m_position + m_halfExtents;
}
