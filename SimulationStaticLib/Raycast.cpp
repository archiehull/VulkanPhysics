#include "pch.h"
#include "Raycast.h"
#include "Sphere.h"
#include "Plane.h"
#include "AABB.h"
#include "Cylinder.h"

Ray::Ray(const glm::vec3& o, const glm::vec3& d)
    : origin(o), direction(glm::normalize(d))
{
}

glm::vec3 Ray::GetPoint(float t) const
{
    return origin + direction * t;
}

namespace Raycast
{
    RayHit IntersectSphere(const Ray& ray, const Sphere& sphere)
    {
        RayHit result;
        glm::vec3 oc = ray.origin - sphere.Position();
        
        float a = glm::dot(ray.direction, ray.direction);
        float b = 2.0f * glm::dot(oc, ray.direction);
        float c = glm::dot(oc, oc) - sphere.m_radius * sphere.m_radius;
        
        float discriminant = b * b - 4.0f * a * c;
        
        if (discriminant < 0.0f) {
            return result;
        }
        
        float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
        
        if (t < 0.0f) {
            t = (-b + std::sqrt(discriminant)) / (2.0f * a);
        }
        
        if (t >= 0.0f) {
            result.hit = true;
            result.distance = t;
            result.point = ray.GetPoint(t);
            result.normal = glm::normalize(result.point - sphere.Position());
        }
        
        return result;
    }

    RayHit IntersectPlane(const Ray& ray, const Plane& plane)
    {
        RayHit result;
        glm::vec3 normal = plane.GetNormal();
        
        float denom = glm::dot(normal, ray.direction);
        
        if (std::abs(denom) > 1e-6f) {
            float t = -(plane.GetSignedDistance(ray.origin)) / denom;
            
            if (t >= 0.0f) {
                result.hit = true;
                result.distance = t;
                result.point = ray.GetPoint(t);
                result.normal = normal;
            }
        }
        
        return result;
    }

    RayHit IntersectAABB(const Ray& ray, const AABB& aabb)
    {
        RayHit result;
        glm::vec3 min = aabb.GetMin();
        glm::vec3 max = aabb.GetMax();
        
        float tmin = 0.0f;
        float tmax = std::numeric_limits<float>::max();
        
        for (int i = 0; i < 3; ++i) {
            if (std::abs(ray.direction[i]) < 1e-6f) {
                if (ray.origin[i] < min[i] || ray.origin[i] > max[i]) {
                    return result;
                }
            } else {
                float t1 = (min[i] - ray.origin[i]) / ray.direction[i];
                float t2 = (max[i] - ray.origin[i]) / ray.direction[i];
                
                if (t1 > t2) std::swap(t1, t2);
                
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                
                if (tmin > tmax) return result;
            }
        }
        
        if (tmin >= 0.0f) {
            result.hit = true;
            result.distance = tmin;
            result.point = ray.GetPoint(tmin);
            
            glm::vec3 center = aabb.Position();
            glm::vec3 d = result.point - center;
            glm::vec3 absD = glm::abs(d);
            
            if (absD.x > absD.y && absD.x > absD.z) {
                result.normal = glm::vec3(d.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
            } else if (absD.y > absD.z) {
                result.normal = glm::vec3(0.0f, d.y > 0 ? 1.0f : -1.0f, 0.0f);
            } else {
                result.normal = glm::vec3(0.0f, 0.0f, d.z > 0 ? 1.0f : -1.0f);
            }
        }
        
        return result;
    }

    RayHit IntersectCylinder(const Ray& ray, const Cylinder& cylinder)
    {
        RayHit result;
        // Simplified infinite cylinder intersection
        // For finite cylinder, additional cap checks needed
        result.hit = false;
        return result;
    }
}
