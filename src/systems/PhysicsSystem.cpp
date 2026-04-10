#include "PhysicsSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include <Sphere.h>
#include <Plane.h>
#include <PhysicsHelper.h>
#include <cmath>
#include <unordered_set>

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

// Drag / damping defaults (disabled by default for behavior stability)
bool PhysicsSystem::applyLinearDamping = true;
float PhysicsSystem::linearDampingFactor = 0.98f;
bool PhysicsSystem::applyQuadraticDrag = true;
float PhysicsSystem::quadraticDragCoefficient = 0.01f;

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

    inline bool IsSphereInsideFinitePlaneBounds(const TransformComponent& planeTransform, const ColliderComponent& planeCollider,
        const glm::vec3& sphereCenter, float sphereRadius) {
        if (planeCollider.radius <= 0.0f || planeCollider.height <= 0.0f) {
            return true; // Infinite plane fallback
        }

        const glm::vec3 n = glm::normalize(planeCollider.normal);
        const glm::vec3 ref = (std::abs(n.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 tangent = glm::normalize(glm::cross(ref, n));
        const glm::vec3 bitangent = glm::normalize(glm::cross(n, tangent));

        const glm::vec3 local = sphereCenter - planeTransform.position;
        const float u = glm::dot(local, tangent);
        const float v = glm::dot(local, bitangent);

        return std::abs(u) <= (planeCollider.radius + sphereRadius) &&
            std::abs(v) <= (planeCollider.height + sphereRadius);
    }
}

void PhysicsSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();
    const float dt = deltaTime / static_cast<float>(subSteps);

    // 1. Move the sub-step loop to wrap ALL physics force applications
    for (int i = 0; i < subSteps; ++i) {
        
        // --- Spring forces: iterate entities with SpringComponent ---
        const Entity entityCount = registry.GetEntityCount();
        for (Entity e = 0; e < entityCount; ++e) {
            if (!registry.HasComponent<SpringComponent>(e) ||
                !registry.HasComponent<TransformComponent>(e) ||
                !registry.HasComponent<PhysicsComponent>(e)) {
                continue;
            }

            auto& spring = registry.GetComponent<SpringComponent>(e);
            auto& transformA = registry.GetComponent<TransformComponent>(e);
            auto& physicsA = registry.GetComponent<PhysicsComponent>(e);

            glm::vec3 posA = transformA.position;
            glm::vec3 velA = physicsA.velocity;

            if (!spring.isAttachedToEntity) {
                glm::vec3 posB = spring.fixedAnchorPoint;
                glm::vec3 velB = glm::vec3(0.0f);

                glm::vec3 deltaPos = posB - posA;
                float distance = glm::length(deltaPos);
                if (distance <= 0.0001f) continue;
                glm::vec3 direction = deltaPos / distance;

                float displacement = distance - spring.restingLength;
                float springForceMagnitude = spring.stiffness * displacement;

                glm::vec3 relativeVelocity = velB - velA;
                float velocityAlongSpring = glm::dot(relativeVelocity, direction);
                float dampingForceMagnitude = spring.damping * velocityAlongSpring;

                glm::vec3 totalForceOnA = direction * (springForceMagnitude + dampingForceMagnitude);
                physicsA.ApplyForce(totalForceOnA);
                continue;
            }

            for (Entity target : spring.connectedEntities) {
                if (target == MAX_ENTITIES || target >= entityCount) continue;
                if (!registry.HasComponent<TransformComponent>(target)) continue;

                glm::vec3 posB = registry.GetComponent<TransformComponent>(target).position;
                glm::vec3 velB = glm::vec3(0.0f);
                if (registry.HasComponent<PhysicsComponent>(target)) {
                    velB = registry.GetComponent<PhysicsComponent>(target).velocity;
                }

                glm::vec3 deltaPos = posB - posA;
                float distance = glm::length(deltaPos);
                if (distance <= 0.0001f) continue;
                glm::vec3 direction = deltaPos / distance;

                float displacement = distance - spring.restingLength;
                float springForceMagnitude = spring.stiffness * displacement;

                glm::vec3 relativeVelocity = velB - velA;
                float velocityAlongSpring = glm::dot(relativeVelocity, direction);
                float dampingForceMagnitude = spring.damping * velocityAlongSpring;

                glm::vec3 totalForceOnA = direction * (springForceMagnitude + dampingForceMagnitude);

                physicsA.ApplyForce(totalForceOnA);
                if (registry.HasComponent<PhysicsComponent>(target)) {
                    auto& physicsB = registry.GetComponent<PhysicsComponent>(target);
                    physicsB.ApplyForce(-totalForceOnA);
                }
            }
        }

        // 2. Integration and Collisions happen AFTER forces are calculated for this step
        Integrate(registry, dt);
        ResolveCollisions(scene, registry, dt);
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

                // Use helper API directly for damping/drag
                float colRadius = 1.0f;
                if (registry.HasComponent<ColliderComponent>(i)) {
                    colRadius = registry.GetComponent<ColliderComponent>(i).radius;
                }

                MovingSphere helperBody(transform.position, colRadius, physics.velocity, physics.inverseMass, physics.restitution);
                helperBody.forceAccumulator = physics.forceAccumulator;
                // Load current orientation from physics component so rotations persist
                helperBody.orientation = physics.orientation;
                // Load rotational state and inertia
                helperBody.angularVelocity = physics.angularVelocity;
                helperBody.torqueAccumulator = physics.torqueAccumulator;
                helperBody.inertiaTensor = physics.inertiaTensor;
                helperBody.inverseInertiaTensor = physics.inverseInertiaTensor;

                if (applyLinearDamping) {
                    ApplyLinearDamping(helperBody, linearDampingFactor, dt);
                }

                if (applyQuadraticDrag) {
                    ApplyQuadraticDrag(helperBody, quadraticDragCoefficient, dt);
                }

                physics.velocity = helperBody.velocity;
                physics.forceAccumulator = helperBody.forceAccumulator;

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

                // --- Angular integration: use torque/inertia to update angular velocity & orientation ---
                IntegrateAngularVelocity(helperBody, dt);
                // Write rotational results back into ECS component
                physics.angularVelocity = helperBody.angularVelocity;
                physics.orientation = helperBody.orientation;
                physics.torqueAccumulator = helperBody.torqueAccumulator; // should be cleared by integrator

                physics.forceAccumulator = glm::vec3(0.0f);

                // Sync orientation + position + scale into transform.matrix for renderer
                glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.position);
                glm::mat4 rotationMat = glm::mat4(physics.orientation);
                glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), transform.scale);
                transform.matrix = translation * rotationMat * scaleMat;
                // Note: Do not call UpdateMatrix() which would overwrite matrix from Euler degrees
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

void PhysicsSystem::ResolveCollisions(Scene& scene, Registry& registry, float dt) {
    const auto entityCount = registry.GetEntityCount();
    bool useForce = (currentResolutionMethod == ResolutionMethod::Force);
    std::unordered_set<Entity> pendingDelete;

    auto queueDespawnerDeletion = [&](Entity a, Entity b, const PhysicsComponent& physA, const PhysicsComponent& physB) {
        const bool aIsDespawner = registry.HasComponent<DespawnerComponent>(a) &&
            registry.GetComponent<DespawnerComponent>(a).enabled;
        const bool bIsDespawner = registry.HasComponent<DespawnerComponent>(b) &&
            registry.GetComponent<DespawnerComponent>(b).enabled;

        if (aIsDespawner && !bIsDespawner && !physB.isStatic) {
            pendingDelete.insert(b);
        }
        if (bIsDespawner && !aIsDespawner && !physA.isStatic) {
            pendingDelete.insert(a);
        }
    };

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
                    queueDespawnerDeletion(i, j, p1, p2);
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

                if (planeB.Intersects(sphereA.sphere) &&
                    IsSphereInsideFinitePlaneBounds(t2, c2, t1.position, c1.radius)) {
                    queueDespawnerDeletion(i, j, p1, p2);
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

                if (planeA.Intersects(sphereB.sphere) &&
                    IsSphereInsideFinitePlaneBounds(t1, c1, t2.position, c2.radius)) {
                    queueDespawnerDeletion(i, j, p1, p2);
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

    for (Entity e : pendingDelete) {
        if (!registry.HasComponent<PhysicsComponent>(e) || !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }
        scene.DeleteEntity(e);
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