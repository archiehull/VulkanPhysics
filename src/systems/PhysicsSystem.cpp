#include "PhysicsSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include "../../SimulationStaticLib/Sphere.h"
#include "../../SimulationStaticLib/Plane.h"
#include "../../SimulationStaticLib/PhysicsHelper.h"

// Default settings
int PhysicsSystem::subSteps = 4;
IntegrationMethod PhysicsSystem::currentMethod = IntegrationMethod::SemiImplicitEuler;
bool PhysicsSystem::applyGravity = true;

void PhysicsSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    // Calculate the fixed timestep for each substep
    float dt = deltaTime / static_cast<float>(subSteps);

    // Run the simulation multiple times per frame
    for (int i = 0; i < subSteps; ++i) {
        Integrate(registry, dt);
        ResolveCollisions(registry);
    }
}

void PhysicsSystem::Integrate(Registry& registry, float dt) {
    for (Entity i = 0; i < registry.GetEntityCount(); ++i) {
        if (registry.HasComponent<TransformComponent>(i) && registry.HasComponent<PhysicsComponent>(i)) {

            auto& transform = registry.GetComponent<TransformComponent>(i);
            auto& physics = registry.GetComponent<PhysicsComponent>(i);

            if (!physics.isStatic && physics.inverseMass > 0.0f) {

                // 1. Accumulate Forces (Gravity: F = mg)
                if (applyGravity) {
                    glm::vec3 gravityForce = glm::vec3(0.0f, -9.81f, 0.0f) * physics.mass;
                    physics.forceAccumulator += gravityForce;
                }

                // 2. Calculate Acceleration (a = F / m)
                glm::vec3 acceleration = physics.forceAccumulator * physics.inverseMass;

                // 3. Integration Methods
                if (currentMethod == IntegrationMethod::ExplicitEuler) {
                    transform.position += physics.velocity * dt;
                    physics.velocity += acceleration * dt;
                }
                else if (currentMethod == IntegrationMethod::SemiImplicitEuler) {
                    physics.velocity += acceleration * dt;
                    transform.position += physics.velocity * dt;
                }
                else if (currentMethod == IntegrationMethod::RK4) {
                    // RK4 Implementation for constant acceleration
                    glm::vec3 k1_v = acceleration;
                    glm::vec3 k1_x = physics.velocity;

                    glm::vec3 k2_v = acceleration; // Assuming constant acceleration over dt
                    glm::vec3 k2_x = physics.velocity + k1_v * (dt * 0.5f);

                    glm::vec3 k3_v = acceleration;
                    glm::vec3 k3_x = physics.velocity + k2_v * (dt * 0.5f);

                    glm::vec3 k4_v = acceleration;
                    glm::vec3 k4_x = physics.velocity + k3_v * dt;

                    physics.velocity += (k1_v + 2.0f * k2_v + 2.0f * k3_v + k4_v) * (dt / 6.0f);
                    transform.position += (k1_x + 2.0f * k2_x + 2.0f * k3_x + k4_x) * (dt / 6.0f);
                }

                // 4. Clear the accumulator for the next frame
                physics.forceAccumulator = glm::vec3(0.0f);

                // (Optional: Apply air resistance here)
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