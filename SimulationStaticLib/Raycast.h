#pragma once
#include <glm/glm.hpp>

struct Ray
{
    glm::vec3 origin;
    glm::vec3 direction;

    Ray(const glm::vec3& o, const glm::vec3& d);
    glm::vec3 GetPoint(float t) const;
};

struct RayHit
{
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 point = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f);
};

class Sphere;
class Plane;
class AABB;
class Cylinder;

namespace Raycast
{
    RayHit IntersectSphere(const Ray& ray, const Sphere& sphere);
    RayHit IntersectPlane(const Ray& ray, const Plane& plane);
    RayHit IntersectAABB(const Ray& ray, const AABB& aabb);
    RayHit IntersectCylinder(const Ray& ray, const Cylinder& cylinder);
}
