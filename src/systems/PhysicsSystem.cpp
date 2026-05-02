#include "PhysicsSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include <Sphere.h>
#include <Plane.h>
#include <PhysicsHelper.h>
#include <cmath>
#include <unordered_set>
#include <vector>

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
bool PhysicsSystem::applySleepNormalThreshold = true;
bool PhysicsSystem::applySleepTangentialThreshold = true;

// Drag / damping defaults (disabled by default for behavior stability)
bool PhysicsSystem::applyLinearDamping = true;
float PhysicsSystem::linearDampingFactor = 0.98f;
bool PhysicsSystem::applyQuadraticDrag = true;
float PhysicsSystem::quadraticDragCoefficient = 0.01f;
bool PhysicsSystem::simulationPaused = false;

void PhysicsSystem::SetLinearDamping(bool enabled, float factor) {
    applyLinearDamping = enabled;
    linearDampingFactor = glm::clamp(factor, 0.0f, 1.0f);
}

void PhysicsSystem::SetQuadraticDrag(bool enabled, float coefficient) {
    applyQuadraticDrag = enabled;
    quadraticDragCoefficient = glm::clamp(coefficient, 0.0f, 10.0f);
}

void PhysicsSystem::SetSleepThresholds(bool normalEnabled, float normalThreshold, bool tangentialEnabled, float tangentialThreshold) {
    applySleepNormalThreshold = normalEnabled;
    sleepNormalThreshold = glm::clamp(normalThreshold, 0.0f, 10.0f);
    applySleepTangentialThreshold = tangentialEnabled;
    sleepTangentialThreshold = glm::clamp(tangentialThreshold, 0.0f, 10.0f);
}

namespace {
    glm::vec3 ClosestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        glm::vec3 ab = b - a;
        glm::vec3 ac = c - a;
        glm::vec3 ap = p - a;

        float d1 = glm::dot(ab, ap);
        float d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return a; // Vertex A

        glm::vec3 bp = p - b;
        float d3 = glm::dot(ab, bp);
        float d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return b; // Vertex B

        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            float v = d1 / (d1 - d3);
            return a + v * ab; // Edge AB
        }

        glm::vec3 cp = p - c;
        float d5 = glm::dot(ab, cp);
        float d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return c; // Vertex C

        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
            float w = d2 / (d2 - d6);
            return a + w * ac; // Edge AC
        }

        float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
            float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return b + w * (c - b); // Edge BC
        }

        float denom = 1.0f / (va + vb + vc);
        float v = vb * denom;
        float w = vc * denom;
        return a + ab * v + ac * w; // Face
    }

    void GetBarycentric(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& u, float& v, float& w) {
        glm::vec3 v0 = b - a, v1 = c - a, v2 = p - a;
        float d00 = glm::dot(v0, v0);
        float d01 = glm::dot(v0, v1);
        float d11 = glm::dot(v1, v1);
        float d20 = glm::dot(v2, v0);
        float d21 = glm::dot(v2, v1);
        float denom = d00 * d11 - d01 * d01;

        if (std::abs(denom) < 1e-8f) { u = 0.333f; v = 0.333f; w = 0.333f; return; }

        v = (d11 * d20 - d01 * d21) / denom;
        w = (d00 * d21 - d01 * d20) / denom;
        u = 1.0f - v - w;
    }

    void ResolveSphereClothTriangleCollision(
        MovingSphere& sphere,
        TransformComponent& tA, TransformComponent& tB, TransformComponent& tC,
        PhysicsComponent& pA, PhysicsComponent& pB, PhysicsComponent& pC)
    {
        glm::vec3 minV = glm::min(tA.position, glm::min(tB.position, tC.position));
        glm::vec3 maxV = glm::max(tA.position, glm::max(tB.position, tC.position));
        glm::vec3 sPos = sphere.sphere.Position();
        float r = sphere.sphere.m_radius;
        if (sPos.x + r < minV.x || sPos.x - r > maxV.x ||
            sPos.y + r < minV.y || sPos.y - r > maxV.y ||
            sPos.z + r < minV.z || sPos.z - r > maxV.z) {
            return;
        }

        glm::vec3 closest = ClosestPointOnTriangle(sphere.sphere.Position(), tA.position, tB.position, tC.position);
        
        glm::vec3 delta = sphere.sphere.Position() - closest;
        float distSq = glm::dot(delta, delta);

        if (distSq > 1e-8f && distSq <= (r * r)) {
            float dist = std::sqrt(distSq);
            glm::vec3 n = delta / dist; 
            float penetration = r - dist;

            float u, v, w;
            GetBarycentric(closest, tA.position, tB.position, tC.position, u, v, w);

            float clothInvMass = (u * u * pA.inverseMass) + (v * v * pB.inverseMass) + (w * w * pC.inverseMass);
            float totalInvMass = sphere.invMass + clothInvMass;
            
            if (totalInvMass <= 0.0f) return;

            if (sphere.invMass > 0.0f) {
                glm::vec3 sphereCorrection = n * (penetration * (sphere.invMass / totalInvMass));
                sphere.sphere.SetPosition(sphere.sphere.Position() + sphereCorrection);
            }
            
            float clothCorrectionWeight = penetration / totalInvMass;
            if (!pA.isStatic) tA.position -= n * (clothCorrectionWeight * u * pA.inverseMass);
            if (!pB.isStatic) tB.position -= n * (clothCorrectionWeight * v * pB.inverseMass);
            if (!pC.isStatic) tC.position -= n * (clothCorrectionWeight * w * pC.inverseMass);

            glm::vec3 clothVel = (u * pA.velocity) + (v * pB.velocity) + (w * pC.velocity);
            
            glm::vec3 rS = -n * r; 
            glm::vec3 sphereVel = sphere.velocity + glm::cross(sphere.angularVelocity, rS);

            glm::vec3 relVel = sphereVel - clothVel;
            float velAlongNormal = glm::dot(relVel, n);

            if (velAlongNormal < 0.0f) {
                float e = 0.1f; 
                
                glm::vec3 rCrossN = glm::cross(rS, n);
                glm::vec3 angularEffect = glm::cross(sphere.inverseInertiaTensor * rCrossN, rS);
                float sphereMassEffect = sphere.invMass + glm::dot(angularEffect, n);
                
                float j = -(1.0f + e) * velAlongNormal / (sphereMassEffect + clothInvMass);
                glm::vec3 impulse = n * j;

                if (sphere.invMass > 0.0f) {
                    sphere.velocity += impulse * sphere.invMass;
                    sphere.angularVelocity += sphere.inverseInertiaTensor * glm::cross(rS, impulse);
                }

                if (!pA.isStatic) pA.velocity -= impulse * (u * pA.inverseMass);
                if (!pB.isStatic) pB.velocity -= impulse * (v * pB.inverseMass);
                if (!pC.isStatic) pC.velocity -= impulse * (w * pC.inverseMass);
            }
        }
    }

    inline void ApplySleepThreshold(PhysicsComponent& phys, const Plane& plane) {
        if (phys.isStatic) return;

        const glm::vec3 n = plane.GetNormal();
        const float vNRaw = glm::dot(phys.velocity, n);
        const float vN = std::abs(vNRaw);
        const glm::vec3 vNVec = vNRaw * n;
        const glm::vec3 vT = phys.velocity - vNVec;
        const float vTLen = glm::length(vT);

        const bool normalAtRest = PhysicsSystem::applySleepNormalThreshold &&
            (vN < PhysicsSystem::sleepNormalThreshold);
        const bool tangentialAtRest = PhysicsSystem::applySleepTangentialThreshold &&
            (vTLen < PhysicsSystem::sleepTangentialThreshold);

        if (!normalAtRest && !tangentialAtRest) {
            return;
        }

        glm::vec3 newVelocity = phys.velocity;
        if (normalAtRest) {
            newVelocity -= vNVec;
        }
        if (tangentialAtRest) {
            newVelocity -= vT;
        }

        phys.velocity = newVelocity;

        if (glm::dot(phys.velocity, phys.velocity) < 1e-8f) {
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
    
    if (simulationPaused) return;

    const float dt = deltaTime / static_cast<float>(subSteps);

    auto springArray = registry.GetComponentArray<SpringComponent>();
    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();

    // 1. Move the sub-step loop to wrap ALL physics force applications
    for (int i = 0; i < subSteps; ++i) {
        
        // --- Spring forces: iterate entities with SpringComponent ---
        const Entity entityCount = registry.GetEntityCount();
        for (Entity e = 0; e < entityCount; ++e) {
            if (!springArray->HasData(e) ||
                !transformArray->HasData(e) ||
                !physicsArray->HasData(e)) {
                continue;
            }

            auto& spring = springArray->GetData(e);
            auto& transformA = transformArray->GetData(e);
            auto& physicsA = physicsArray->GetData(e);

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

            for (size_t j = 0; j < spring.connectedEntities.size(); ++j) {
                Entity target = spring.connectedEntities[j];
                if (target == MAX_ENTITIES || target >= entityCount || !registry.IsAlive(target)) continue;
                if (!transformArray->HasData(target)) continue;

                glm::vec3 posB = transformArray->GetData(target).position;
                glm::vec3 velB = glm::vec3(0.0f);
                if (physicsArray->HasData(target)) {
                    velB = physicsArray->GetData(target).velocity;
                }

                glm::vec3 deltaPos = posB - posA;
                float distance = glm::length(deltaPos);
                if (distance <= 0.0001f) continue;
                glm::vec3 direction = deltaPos / distance;

                float currentRestingLength = (spring.restingLengths.size() > j) ? spring.restingLengths[j] : spring.restingLength;
                float displacement = distance - currentRestingLength;
                float springForceMagnitude = spring.stiffness * displacement;

                glm::vec3 relativeVelocity = velB - velA;
                float velocityAlongSpring = glm::dot(relativeVelocity, direction);
                float dampingForceMagnitude = spring.damping * velocityAlongSpring;

                glm::vec3 totalForceOnA = direction * (springForceMagnitude + dampingForceMagnitude);

                physicsA.ApplyForce(totalForceOnA);
                if (physicsArray->HasData(target)) {
                    auto& physicsB = physicsArray->GetData(target);
                    physicsB.ApplyForce(-totalForceOnA);
                }
            }
        }

        // 2. Integration and Collisions happen AFTER forces are calculated for this step
        Integrate(registry, dt);
        ResolveCollisions(scene, registry, dt);
    }

    // 3. Update transforms once per frame after all sub-steps
    const Entity entityCount = registry.GetEntityCount();
    for (Entity i = 0; i < entityCount; ++i) {
        if (transformArray->HasData(i) && physicsArray->HasData(i)) {
            auto& transform = transformArray->GetData(i);
            auto& physics = physicsArray->GetData(i);
            if (!physics.isStatic) {
                glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.position);
                glm::mat4 rotationMat = glm::mat4(physics.orientation);
                glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), transform.scale);
                transform.matrix = translation * rotationMat * scaleMat;
            }
        }
    }
}

void PhysicsSystem::Integrate(Registry& registry, float dt) {
    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();
    auto colliderArray = registry.GetComponentArray<ColliderComponent>();

    for (Entity i = 0; i < registry.GetEntityCount(); ++i) {
        if (transformArray->HasData(i) && physicsArray->HasData(i)) {
            auto& transform = transformArray->GetData(i);
            auto& physics = physicsArray->GetData(i);

            if (!physics.isStatic && physics.inverseMass > 0.0f) {
                if (applyGravity) {
                    const glm::vec3 gravityForce = glm::vec3(0.0f, 9.81f * gravityDirection, 0.0f) * physics.mass;
                    physics.forceAccumulator += gravityForce;
                }

                float colRadius = 1.0f;
                int colType = 0;
                float colHeight = 5.0f;
                if (colliderArray->HasData(i)) {
                    const auto& col = colliderArray->GetData(i);
                    colRadius = col.radius;
                    colType = col.type;
                    colHeight = col.height;
                }

                if (colType == 2) {
                    glm::vec3 up(0, 1, 0);
                    up = physics.orientation * up;
                    glm::vec3 p1 = transform.position - up * (colHeight * 0.5f);
                    glm::vec3 p2 = transform.position + up * (colHeight * 0.5f);
                    Capsule cap(p1, p2, colRadius);
                    MovingCapsule helperBody(cap, physics.velocity, physics.inverseMass, physics.restitution);
                    helperBody.forceAccumulator = physics.forceAccumulator;
                    helperBody.orientation = physics.orientation;
                    helperBody.angularVelocity = physics.angularVelocity;
                    helperBody.torqueAccumulator = physics.torqueAccumulator;
                    helperBody.inertiaTensor = physics.inertiaTensor;
                    helperBody.inverseInertiaTensor = physics.inverseInertiaTensor;

                    if (applyLinearDamping) ApplyLinearDamping(helperBody, linearDampingFactor, dt);
                    if (applyQuadraticDrag) ApplyQuadraticDrag(helperBody, quadraticDragCoefficient, dt);

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

                    IntegrateAngularVelocity(helperBody, dt);
                    physics.angularVelocity = helperBody.angularVelocity;
                    physics.orientation = helperBody.orientation;
                    physics.torqueAccumulator = helperBody.torqueAccumulator;
                } else {
                    MovingSphere helperBody(transform.position, colRadius, physics.velocity, physics.inverseMass, physics.restitution);
                    helperBody.forceAccumulator = physics.forceAccumulator;
                    helperBody.orientation = physics.orientation;
                    helperBody.angularVelocity = physics.angularVelocity;
                    helperBody.torqueAccumulator = physics.torqueAccumulator;
                    helperBody.inertiaTensor = physics.inertiaTensor;
                    helperBody.inverseInertiaTensor = physics.inverseInertiaTensor;

                    if (applyLinearDamping) ApplyLinearDamping(helperBody, linearDampingFactor, dt);
                    if (applyQuadraticDrag) ApplyQuadraticDrag(helperBody, quadraticDragCoefficient, dt);

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

                    IntegrateAngularVelocity(helperBody, dt);
                    physics.angularVelocity = helperBody.angularVelocity;
                    physics.orientation = helperBody.orientation;
                    physics.torqueAccumulator = helperBody.torqueAccumulator;
                }

                physics.forceAccumulator = glm::vec3(0.0f);
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

    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto colliderArray = registry.GetComponentArray<ColliderComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();
    auto despawnerArray = registry.GetComponentArray<DespawnerComponent>();

    auto queueDespawnerDeletion = [&](Entity a, Entity b, const PhysicsComponent& physA, const PhysicsComponent& physB) {
        const bool aIsDespawner = despawnerArray->HasData(a) && despawnerArray->GetData(a).enabled;
        const bool bIsDespawner = despawnerArray->HasData(b) && despawnerArray->GetData(b).enabled;

        auto canDespawn = [&](Entity e) {
            return !registry.HasComponent<SpringComponent>(e);
        };

        if (aIsDespawner && !bIsDespawner && !physB.isStatic && canDespawn(b)) {
            pendingDelete.insert(b);
        }
        if (bIsDespawner && !aIsDespawner && !physA.isStatic && canDespawn(a)) {
            pendingDelete.insert(a);
        }
    };

    // Phase 2 Optimization: Dense active colliders list
    std::vector<Entity> activeColliders;
    activeColliders.reserve(entityCount);
    for (Entity i = 0; i < entityCount; ++i) {
        if (transformArray->HasData(i) && colliderArray->HasData(i) && physicsArray->HasData(i)) {
            if (colliderArray->GetData(i).hasCollision) {
                activeColliders.push_back(i);
            }
        }
    }

    const size_t activeCount = activeColliders.size();
    for (size_t iIdx = 0; iIdx < activeCount; ++iIdx) {
        Entity i = activeColliders[iIdx];
        for (size_t jIdx = iIdx + 1; jIdx < activeCount; ++jIdx) {
            Entity j = activeColliders[jIdx];

            auto& t1 = transformArray->GetData(i);
            auto& c1 = colliderArray->GetData(i);
            auto& p1 = physicsArray->GetData(i);

            auto& t2 = transformArray->GetData(j);
            auto& c2 = colliderArray->GetData(j);
            auto& p2 = physicsArray->GetData(j);

            // Collision Filtering
            if ((c1.collisionLayer & c2.collisionMask) == 0 || 
                (c2.collisionLayer & c1.collisionMask) == 0) {
                continue;
            }

            // Sphere vs Sphere
            if (c1.type == 0 && c2.type == 0) { // Sphere vs Sphere
                if (c1.isClothParticle != c2.isClothParticle) continue;

                MovingSphere sphereA(t1.position, c1.radius, p1.velocity, p1.inverseMass, p1.restitution);
                sphereA.forceAccumulator = p1.forceAccumulator; // Load accumulator
                sphereA.torqueAccumulator = p1.torqueAccumulator;
                sphereA.angularVelocity = p1.angularVelocity;
                sphereA.inertiaTensor = p1.inertiaTensor;
                sphereA.inverseInertiaTensor = p1.inverseInertiaTensor;

                MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.inverseMass, p2.restitution);
                sphereB.forceAccumulator = p2.forceAccumulator; // Load accumulator
                sphereB.torqueAccumulator = p2.torqueAccumulator;
                sphereB.angularVelocity = p2.angularVelocity;
                sphereB.inertiaTensor = p2.inertiaTensor;
                sphereB.inverseInertiaTensor = p2.inverseInertiaTensor;

                if (sphereA.sphere.CollideWith(sphereB.sphere)) {
                    queueDespawnerDeletion(i, j, p1, p2);
                    const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);
                    ResolveElasticCollision(sphereA, sphereB, useForce, dt, contactFriction);

                    // Write results back to ECS components
                    p1.velocity = sphereA.velocity;
                    p2.velocity = sphereB.velocity;
                    p1.angularVelocity = sphereA.angularVelocity;
                    p2.angularVelocity = sphereB.angularVelocity;
                    p1.forceAccumulator = sphereA.forceAccumulator;
                    p2.forceAccumulator = sphereB.forceAccumulator;
                    p1.torqueAccumulator = sphereA.torqueAccumulator;
                    p2.torqueAccumulator = sphereB.torqueAccumulator;

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
                sphereA.angularVelocity = p1.angularVelocity;
                sphereA.inertiaTensor = p1.inertiaTensor;
                sphereA.inverseInertiaTensor = p1.inverseInertiaTensor;
                Plane planeB(t2.position, c2.normal, c2.radius);

                if (planeB.Intersects(sphereA.sphere) &&
                    IsSphereInsideFinitePlaneBounds(t2, c2, t1.position, c1.radius)) {
                    queueDespawnerDeletion(i, j, p1, p2);
                    const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);

                    ResolveSpherePlaneCollision(sphereA, planeB, p2.restitution, contactFriction);

                    if (!p1.isStatic) {
                        p1.velocity = sphereA.velocity;
                        p1.angularVelocity = sphereA.angularVelocity;
                        ApplySpherePlaneCorrection(t1, c1.radius, planeB);
                        ApplySleepThreshold(p1, planeB);
                    }
                }
            }
            // Plane vs Sphere
            else if (c1.type == 1 && c2.type == 0) {
                MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.inverseMass, p2.restitution);
                sphereB.angularVelocity = p2.angularVelocity;
                sphereB.inertiaTensor = p2.inertiaTensor;
                sphereB.inverseInertiaTensor = p2.inverseInertiaTensor;
                Plane planeA(t1.position, c1.normal, c1.radius);

                if (planeA.Intersects(sphereB.sphere) &&
                    IsSphereInsideFinitePlaneBounds(t1, c1, t2.position, c2.radius)) {
                    queueDespawnerDeletion(i, j, p1, p2);
                    const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);

                    ResolveSpherePlaneCollision(sphereB, planeA, p1.restitution, contactFriction);

                    if (!p2.isStatic) {
                        p2.velocity = sphereB.velocity;
                        p2.angularVelocity = sphereB.angularVelocity;
                        ApplySpherePlaneCorrection(t2, c2.radius, planeA);
                        ApplySleepThreshold(p2, planeA);
                    }
                }
            }
            // Capsule vs Plane
            else if (c1.type == 2 && c2.type == 1) {
                glm::vec3 up = p1.orientation * glm::vec3(0, 1, 0);
                glm::vec3 p1_local = t1.position - up * (c1.height * 0.5f);
                glm::vec3 p2_local = t1.position + up * (c1.height * 0.5f);
                Capsule capA(p1_local, p2_local, c1.radius);
                
                MovingCapsule capsuleA(capA, p1.velocity, p1.inverseMass, p1.restitution);
                capsuleA.angularVelocity = p1.angularVelocity;
                capsuleA.inertiaTensor = p1.inertiaTensor;
                capsuleA.inverseInertiaTensor = p1.inverseInertiaTensor;
                capsuleA.orientation = p1.orientation;

                Plane planeB(t2.position, c2.normal, c2.radius);

                if (capA.Intersects(planeB)) {
                    queueDespawnerDeletion(i, j, p1, p2);
                    const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);

                    ResolveCapsulePlaneCollision(capsuleA, planeB, p2.restitution, contactFriction);

                    if (!p1.isStatic) {
                        p1.velocity = capsuleA.velocity;
                        p1.angularVelocity = capsuleA.angularVelocity;
                        
                        float d1 = planeB.GetSignedDistance(p1_local);
                        float d2 = planeB.GetSignedDistance(p2_local);
                        float minDist = std::min(d1, d2);
                        float overlap = c1.radius - minDist;
                        if (overlap > 0.0f) {
                            t1.position += planeB.GetNormal() * overlap;
                            t1.UpdateMatrix();
                        }
                        ApplySleepThreshold(p1, planeB);
                    }
                }
            }
            // Plane vs Capsule
            else if (c1.type == 1 && c2.type == 2) {
                glm::vec3 up = p2.orientation * glm::vec3(0, 1, 0);
                glm::vec3 p1_local = t2.position - up * (c2.height * 0.5f);
                glm::vec3 p2_local = t2.position + up * (c2.height * 0.5f);
                Capsule capB(p1_local, p2_local, c2.radius);
                
                MovingCapsule capsuleB(capB, p2.velocity, p2.inverseMass, p2.restitution);
                capsuleB.angularVelocity = p2.angularVelocity;
                capsuleB.inertiaTensor = p2.inertiaTensor;
                capsuleB.inverseInertiaTensor = p2.inverseInertiaTensor;
                capsuleB.orientation = p2.orientation;

                Plane planeA(t1.position, c1.normal, c1.radius);

                if (capB.Intersects(planeA)) {
                    queueDespawnerDeletion(i, j, p1, p2);
                    const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);

                    ResolveCapsulePlaneCollision(capsuleB, planeA, p1.restitution, contactFriction);

                    if (!p2.isStatic) {
                        p2.velocity = capsuleB.velocity;
                        p2.angularVelocity = capsuleB.angularVelocity;
                        
                        float d1 = planeA.GetSignedDistance(p1_local);
                        float d2 = planeA.GetSignedDistance(p2_local);
                        float minDist = std::min(d1, d2);
                        float overlap = c2.radius - minDist;
                        if (overlap > 0.0f) {
                            t2.position += planeA.GetNormal() * overlap;
                            t2.UpdateMatrix();
                        }
                        ApplySleepThreshold(p2, planeA);
                    }
                }
            }
        }
    }

    auto clothArray = registry.GetComponentArray<ClothComponent>();
    
    std::vector<Entity> activeSpheres;
    for (size_t iIdx = 0; iIdx < activeCount; ++iIdx) {
        Entity i = activeColliders[iIdx];
        if (colliderArray->GetData(i).type == 0) { // 0 = Sphere
            activeSpheres.push_back(i);
        }
    }

    for (Entity c = 0; c < entityCount; ++c) {
        if (!clothArray->HasData(c)) continue;
        
        auto& cloth = clothArray->GetData(c);
        if (!cloth.collisionsEnabled || !cloth.dynamicGeometry || !cloth.dynamicGeometry->HasIndices()) continue;

        const auto& indices = cloth.dynamicGeometry->GetIndices();
        
        std::unordered_set<Entity> clothParticleSet(cloth.particles.begin(), cloth.particles.end());

        for (Entity s : activeSpheres) {
            if (clothParticleSet.find(s) != clothParticleSet.end()) continue;

            auto& sphereTrans = transformArray->GetData(s);
            auto& sphereCol = colliderArray->GetData(s);
            auto& spherePhys = physicsArray->GetData(s);

            MovingSphere helperSphere(sphereTrans.position, sphereCol.radius, spherePhys.velocity, spherePhys.inverseMass, spherePhys.restitution);
            helperSphere.angularVelocity = spherePhys.angularVelocity;
            helperSphere.inertiaTensor = spherePhys.inertiaTensor;
            helperSphere.inverseInertiaTensor = spherePhys.inverseInertiaTensor;

            for (size_t i = 0; i < indices.size(); i += 3) {
                Entity pA_ent = cloth.particles[indices[i]];
                Entity pB_ent = cloth.particles[indices[i+1]];
                Entity pC_ent = cloth.particles[indices[i+2]];

                if (pA_ent == MAX_ENTITIES || !transformArray->HasData(pA_ent) || !physicsArray->HasData(pA_ent)) continue;
                if (pB_ent == MAX_ENTITIES || !transformArray->HasData(pB_ent) || !physicsArray->HasData(pB_ent)) continue;
                if (pC_ent == MAX_ENTITIES || !transformArray->HasData(pC_ent) || !physicsArray->HasData(pC_ent)) continue;

                auto& tA = transformArray->GetData(pA_ent);
                auto& tB = transformArray->GetData(pB_ent);
                auto& tC = transformArray->GetData(pC_ent);
                
                auto& pA = physicsArray->GetData(pA_ent);
                auto& pB = physicsArray->GetData(pB_ent);
                auto& pC = physicsArray->GetData(pC_ent);

                ResolveSphereClothTriangleCollision(helperSphere, tA, tB, tC, pA, pB, pC);
            }

            if (!spherePhys.isStatic) {
                sphereTrans.position = helperSphere.sphere.Position();
                sphereTrans.UpdateMatrix();
                spherePhys.velocity = helperSphere.velocity;
                spherePhys.angularVelocity = helperSphere.angularVelocity;
            }
        }
    }

    for (Entity e : pendingDelete) {
        if (!physicsArray->HasData(e) || !transformArray->HasData(e)) {
            continue;
        }
        if (scene.IsLookaheadMode()) {
            scene.DeactivateEntityForLookahead(e);
        } else {
            scene.DeleteEntity(e);
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
