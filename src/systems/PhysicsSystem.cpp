#include "PhysicsSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include <Sphere.h>
#include <Plane.h>
#include <PhysicsHelper.h>
#include <cmath>

// Default settings
int PhysicsSystem::subSteps = 4;
IntegrationMethod PhysicsSystem::currentMethod = IntegrationMethod::SemiImplicitEuler;
ResolutionMethod PhysicsSystem::currentResolutionMethod = ResolutionMethod::Impulse;
bool PhysicsSystem::applyGravity = true;
float PhysicsSystem::gravityDirection = -1.0f;

// Global tuning knobs
float PhysicsSystem::contactFrictionScale = 1.0f;
float PhysicsSystem::sleepNormalThreshold = 0.08f;
float PhysicsSystem::sleepTangentialThreshold = 0.12f;

namespace {
    inline void ApplySleepThreshold(PhysicsComponent& phys, const Plane& plane) {
        if (phys.isStatic) return;

        const glm::vec3 n = plane.GetNormal();
        const float vN = std::abs(glm::dot(phys.velocity, n));
        const glm::vec3 vT = phys.velocity - glm::dot(phys.velocity, n) * n;
        const float vTLen = glm::length(vT);

        if (vN < PhysicsSystem::sleepNormalThreshold &&
            vTLen < PhysicsSystem::sleepTangentialThreshold) {
            phys.velocity = glm::vec3(0.0f);
            phys.forceAccumulator = glm::vec3(0.0f);
        }
    }
}

void PhysicsSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    const float dt = deltaTime / static_cast<float>(subSteps);

    for (int i = 0; i < subSteps; ++i) {
        Integrate(registry, dt);
        ResolveCollisions(registry, dt);
    }
}

void PhysicsSystem::Integrate(Registry& registry, float dt) {
    for (Entity i = 0; i < registry.GetEntityCount(); ++i) {
        if (registry.HasComponent<TransformComponent>(i) && registry.HasComponent<PhysicsComponent>(i)) {
            auto& transform = registry.GetComponent<TransformComponent>(i);
            auto& physics = registry.GetComponent<PhysicsComponent>(i);

            if (!physics.isStatic && physics.inverseMass > 0.0f) {
                if (applyGravity) {
                    const glm::vec3 gravityForce = glm::vec3(0.0f, 9.81f * gravityDirection, 0.0f) * physics.mass;
                    physics.forceAccumulator += gravityForce;
                }

                const glm::vec3 acceleration = physics.forceAccumulator * physics.inverseMass;

                if (currentMethod == IntegrationMethod::ExplicitEuler) {
                    transform.position += physics.velocity * dt;
                    physics.velocity += acceleration * dt;
                }
                else if (currentMethod == IntegrationMethod::SemiImplicitEuler) {
                    physics.velocity += acceleration * dt;
                    transform.position += physics.velocity * dt;
                }
                else if (currentMethod == IntegrationMethod::RK4) {
                    const glm::vec3 k1_v = acceleration;
                    const glm::vec3 k1_x = physics.velocity;

                    const glm::vec3 k2_v = acceleration;
                    const glm::vec3 k2_x = physics.velocity + k1_v * (dt * 0.5f);

                    const glm::vec3 k3_v = acceleration;
                    const glm::vec3 k3_x = physics.velocity + k2_v * (dt * 0.5f);

                    const glm::vec3 k4_v = acceleration;
                    const glm::vec3 k4_x = physics.velocity + k3_v * dt;

                    physics.velocity += (k1_v + 2.0f * k2_v + 2.0f * k3_v + k4_v) * (dt / 6.0f);
                    transform.position += (k1_x + 2.0f * k2_x + 2.0f * k3_x + k4_x) * (dt / 6.0f);
                }

                physics.forceAccumulator = glm::vec3(0.0f);

                // Optional air resistance
                physics.velocity *= std::pow(0.999f, dt * 60.0f);

                transform.UpdateMatrix();
            }
        }
    }
}

void PhysicsSystem::ApplySpherePlaneCorrection(TransformComponent& sphereTrans, float radius, const Plane& plane) {
    const float dist = plane.GetSignedDistance(sphereTrans.position);
    const float overlap = radius - dist;
    if (overlap > 0.0f) {
        sphereTrans.position += plane.GetNormal() * overlap;
        sphereTrans.UpdateMatrix();
    }
}

void PhysicsSystem::ResolveCollisions(Registry& registry, float dt) {
    const auto entityCount = registry.GetEntityCount();
    bool useForce = (currentResolutionMethod == ResolutionMethod::Force);

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
            if (c1.type == 0 && c2.type == 0) { // Sphere vs Sphere
                MovingSphere sphereA(t1.position, c1.radius, p1.velocity, p1.inverseMass, p1.restitution);
                sphereA.forceAccumulator = p1.forceAccumulator; // Load accumulator

                MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.inverseMass, p2.restitution);
                sphereB.forceAccumulator = p2.forceAccumulator; // Load accumulator

                if (sphereA.sphere.CollideWith(sphereB.sphere)) {
                    ResolveElasticCollision(sphereA, sphereB, useForce, dt);

                    // Write results back to ECS components
                    p1.velocity = sphereA.velocity;
                    p2.velocity = sphereB.velocity;
                    p1.forceAccumulator = sphereA.forceAccumulator;
                    p2.forceAccumulator = sphereB.forceAccumulator;

                    if (!useForce) {
                        // Only correct position immediately if we are using instantaneous impulses. 
                        // If using forces, resolving overlap here might interfere with the integration step.
                        ApplyPositionCorrection(t1, t2, c1.radius, c2.radius, p1.isStatic, p2.isStatic);
                    }
                }
            }
            // Sphere vs Plane
            else if (c1.type == 0 && c2.type == 1) {
                MovingSphere sphereA(t1.position, c1.radius, p1.velocity, p1.inverseMass, p1.restitution);
                Plane planeB(t2.position, c2.normal, c2.radius);

                if (planeB.Intersects(sphereA.sphere)) {
                    const float objectFriction = (p1.friction + p2.friction) * 0.5f;
                    const float contactFriction = glm::clamp(objectFriction * PhysicsSystem::contactFrictionScale, 0.0f, 1.0f);

                    ResolveSpherePlaneCollision(sphereA, planeB, p2.restitution, contactFriction);

                    if (!p1.isStatic) {
                        p1.velocity = sphereA.velocity;
                        ApplySpherePlaneCorrection(t1, c1.radius, planeB);
                        ApplySleepThreshold(p1, planeB);
                    }
                }
            }
            // Plane vs Sphere
            else if (c1.type == 1 && c2.type == 0) {
                MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.inverseMass, p2.restitution);
                Plane planeA(t1.position, c1.normal, c1.radius);

                if (planeA.Intersects(sphereB.sphere)) {
                    const float objectFriction = (p1.friction + p2.friction) * 0.5f;
                    const float contactFriction = glm::clamp(objectFriction * PhysicsSystem::contactFrictionScale, 0.0f, 1.0f);

                    ResolveSpherePlaneCollision(sphereB, planeA, p1.restitution, contactFriction);

                    if (!p2.isStatic) {
                        p2.velocity = sphereB.velocity;
                        ApplySpherePlaneCorrection(t2, c2.radius, planeA);
                        ApplySleepThreshold(p2, planeA);
                    }
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

    if (dist == 0.0f) {
        delta = glm::vec3(0.0f, 1.0f, 0.0f);
        dist = 0.0001f;
    }

    if (overlap > 0.0f) {
        const glm::vec3 separation = glm::normalize(delta) * overlap;

        if (!static1 && !static2) {
            t1.position -= separation * 0.5f;
            t2.position += separation * 0.5f;
        }
        else if (!static1 && static2) {
            t1.position -= separation;
        }
        else if (static1 && !static2) {
            t2.position += separation;
        }

        if (!static1) t1.UpdateMatrix();
        if (!static2) t2.UpdateMatrix();
    }
}