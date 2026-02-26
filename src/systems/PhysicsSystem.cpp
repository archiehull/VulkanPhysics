#include "PhysicsSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"                     
#include "../../SimulationStaticLib/Sphere.h"          
#include "../../SimulationStaticLib/PhysicsHelper.h"   
#include "../../SimulationStaticLib/Plane.h"

void PhysicsSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry(); // Extract registry from the scene

    Integrate(registry, deltaTime);
    ResolveCollisions(registry);
}

void PhysicsSystem::Integrate(Registry& registry, float dt) {
    for (Entity i = 0; i < registry.GetEntityCount(); ++i) {
        if (registry.HasComponent<TransformComponent>(i) &&
            registry.HasComponent<PhysicsComponent>(i)) {

            auto& transform = registry.GetComponent<TransformComponent>(i);
            auto& physics = registry.GetComponent<PhysicsComponent>(i);

            if (!physics.isStatic) {
                // Apply Gravity
                physics.velocity.y -= 9.81f * dt;

                // Apply velocity
                transform.position += physics.velocity * dt;

                // FIX: Use 0.999f for light air drag instead of heavy friction
                physics.velocity *= std::pow(0.999f, dt * 60.0f);

                transform.UpdateMatrix();
            }
        }
    }
}

void PhysicsSystem::ApplySpherePlaneCorrection(TransformComponent& sphereTrans, float radius, const Plane& plane) {
    float dist = plane.GetSignedDistance(sphereTrans.position);
    float overlap = radius - dist;
    if (overlap > 0.0f) {
        sphereTrans.position += plane.GetNormal() * overlap;
        sphereTrans.UpdateMatrix();
    }
}

void PhysicsSystem::ResolveCollisions(Registry& registry) {
    auto entityCount = registry.GetEntityCount();
    for (Entity i = 0; i < entityCount; ++i) {
        for (Entity j = i + 1; j < entityCount; ++j) {
            if (!IsCollidable(registry, i) || !IsCollidable(registry, j)) continue;

            auto& t1 = registry.GetComponent<TransformComponent>(i);
            auto& c1 = registry.GetComponent<ColliderComponent>(i);
            auto& p1 = registry.GetComponent<PhysicsComponent>(i);

            auto& t2 = registry.GetComponent<TransformComponent>(j);
            auto& c2 = registry.GetComponent<ColliderComponent>(j);
            auto& p2 = registry.GetComponent<PhysicsComponent>(j);

            // Sphere vs Sphere
            if (c1.type == 0 && c2.type == 0) {
                MovingSphere sphereA(t1.position, c1.radius, p1.velocity, p1.mass, p1.restitution);
                MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.mass, p2.restitution);

                if (sphereA.sphere.CollideWith(sphereB.sphere)) {
                    ResolveElasticCollision(sphereA, sphereB);
                    if (!p1.isStatic) p1.velocity = sphereA.velocity;
                    if (!p2.isStatic) p2.velocity = sphereB.velocity;
                    ApplyPositionCorrection(t1, t2, c1.radius, c2.radius, p1.isStatic, p2.isStatic);
                }
            }
            // Sphere vs Plane
            else if (c1.type == 0 && c2.type == 1) {
                MovingSphere sphereA(t1.position, c1.radius, p1.velocity, p1.mass, p1.restitution);
                Plane planeB(t2.position, c2.normal, c2.radius);

                if (planeB.Intersects(sphereA.sphere)) {
                    ResolveSpherePlaneCollision(sphereA, planeB, p2.restitution);
                    if (!p1.isStatic) p1.velocity = sphereA.velocity;
                    if (!p1.isStatic) ApplySpherePlaneCorrection(t1, c1.radius, planeB);
                }
            }
            // Plane vs Sphere
            else if (c1.type == 1 && c2.type == 0) {
                MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.mass, p2.restitution);
                Plane planeA(t1.position, c1.normal, c1.radius); 

                if (planeA.Intersects(sphereB.sphere)) {
                    ResolveSpherePlaneCollision(sphereB, planeA, p1.restitution);
                    if (!p2.isStatic) p2.velocity = sphereB.velocity;
                    if (!p2.isStatic) ApplySpherePlaneCorrection(t2, c2.radius, planeA);
                }
            }
        }
    }
}

bool PhysicsSystem::IsCollidable(const Registry& reg, Entity e) {
    return reg.HasComponent<TransformComponent>(e) &&
        reg.HasComponent<ColliderComponent>(e) &&
        reg.HasComponent<PhysicsComponent>(e);
}

void PhysicsSystem::ApplyPositionCorrection(TransformComponent& t1, TransformComponent& t2, float r1, float r2, bool static1, bool static2) {
    glm::vec3 delta = t2.position - t1.position;
    float dist = glm::length(delta);
    float overlap = (r1 + r2) - dist;

    // Prevent divide-by-zero if perfectly overlapping
    if (dist == 0.0f) {
        delta = glm::vec3(0.0f, 1.0f, 0.0f);
        dist = 0.0001f;
    }

    if (overlap > 0) {
        glm::vec3 separation = glm::normalize(delta) * overlap;

        if (!static1 && !static2) {
            // Both move half the distance
            t1.position -= separation * 0.5f;
            t2.position += separation * 0.5f;
        }
        else if (!static1 && static2) {
            // Only object 1 moves (full distance)
            t1.position -= separation;
        }
        else if (static1 && !static2) {
            // Only object 2 moves (full distance)
            t2.position += separation;
        }

        if (!static1) t1.UpdateMatrix();
        if (!static2) t2.UpdateMatrix();
    }
}