#include "PhysicsSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include <Sphere.h>
#include <Plane.h>
#include <AABB.h>
#include <PhysicsHelper.h>
#include <cmath>
#include <unordered_set>
#include <vector>
#include <glm/gtc/quaternion.hpp>

int PhysicsSystem::localPeerId = -1;
bool PhysicsSystem::activePeers[4] = { true, false, false, false };

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
static int s_physicsFrameCount = 0;

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
                
                // Safety check: Avoid infinite impulses from division by near-zero mass or precision errors
                if (std::isnan(j) || std::isinf(j) || j > 1000.0f) {
                    j = 0.0f;
                }

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

    void ResolveSphereInsideAABB(MovingSphere& sphere,
        const glm::vec3& boxCenter,
        const glm::vec3& boxHalfExtents,
        const glm::mat3& boxOrientation,
        const glm::vec3& boxLinearVelocity,
        const glm::vec3& boxAngularVelocity,
        float restitution,
        float friction) {
        const float r = sphere.sphere.m_radius;
        const glm::mat3 invBoxRot = glm::transpose(boxOrientation);

        glm::vec3 worldPos = sphere.sphere.Position();
        glm::vec3 localPos = invBoxRot * (worldPos - boxCenter);
        const glm::vec3 localHalfExtents = glm::abs(boxHalfExtents);

        for (int i = 0; i < 3; ++i) {
            glm::vec3 localNormal(0.0f);
            float penetration = 0.0f;

            if (localPos[i] - r < -localHalfExtents[i]) {
                localNormal[i] = 1.0f;
                penetration = -localHalfExtents[i] - (localPos[i] - r);
            }
            else if (localPos[i] + r > localHalfExtents[i]) {
                localNormal[i] = -1.0f;
                penetration = (localPos[i] + r) - localHalfExtents[i];
            }

            if (penetration <= 0.0f) {
                continue;
            }

            const glm::vec3 worldNormal = glm::normalize(boxOrientation * localNormal);

            glm::vec3 localContactPoint = localPos;
            localContactPoint[i] = (localNormal[i] > 0.0f) ? -localHalfExtents[i] : localHalfExtents[i];
            const glm::vec3 worldContactPoint = boxCenter + (boxOrientation * localContactPoint);

            const glm::vec3 pointOffset = worldContactPoint - boxCenter;
            const glm::vec3 wallVelocityAtContact = boxLinearVelocity + glm::cross(boxAngularVelocity, pointOffset);

            Plane wallPlane(worldContactPoint, worldNormal);
            ResolveSpherePlaneCollision(sphere, wallPlane, restitution, friction, wallVelocityAtContact);

            worldPos = sphere.sphere.Position() + (worldNormal * penetration);
            sphere.sphere.SetPosition(worldPos);
            localPos = invBoxRot * (worldPos - boxCenter);
        }
    }

    void ResolveSphereInsideCapsule(MovingSphere& sphere, 
        const glm::vec3& capCenter, 
        float capRadius, 
        float capHeight, 
        const glm::mat3& capOrientation,
        const glm::vec3& capLinearVel,
        const glm::vec3& capAngularVel,
        float restitution, 
        float friction) {
        
        glm::vec3 pos = sphere.sphere.Position();
        float r = sphere.sphere.m_radius;

        // Calculate capsule segment in world space
        glm::vec3 up = capOrientation * glm::vec3(0, 1, 0);
        float halfSegmentLen = std::max(0.0f, (capHeight - 2.0f * capRadius) * 0.5f);
        glm::vec3 p1 = capCenter - up * halfSegmentLen;
        glm::vec3 p2 = capCenter + up * halfSegmentLen;

        // Closest point on segment p1p2 to pos
        glm::vec3 v = p2 - p1;
        float vLenSq = glm::dot(v, v);
        glm::vec3 closestPointOnSegment;
        if (vLenSq < 1e-8f) {
            closestPointOnSegment = p1;
        } else {
            glm::vec3 w = pos - p1;
            float t = glm::dot(w, v) / vLenSq;
            t = glm::clamp(t, 0.0f, 1.0f);
            closestPointOnSegment = p1 + t * v;
        }

        glm::vec3 delta = pos - closestPointOnSegment;
        float dist = glm::length(delta);
        float maxAllowedDist = capRadius - r;

        if (dist > maxAllowedDist && dist > 0.0001f) {
            glm::vec3 normal = -delta / dist; // Points towards segment

            glm::vec3 contactPoint = closestPointOnSegment + (delta / dist) * capRadius;
            
            // Calculate wall velocity at contact point
            glm::vec3 pointOffset = contactPoint - capCenter;
            glm::vec3 wallVel = capLinearVel + glm::cross(capAngularVel, pointOffset);

            Plane wallPlane(contactPoint, normal);
            ResolveSpherePlaneCollision(sphere, wallPlane, restitution, friction, wallVel);

            // Correct position
            sphere.sphere.SetPosition(closestPointOnSegment + (delta / dist) * maxAllowedDist);
        }
    }

    void ResolveSphereInsideSphere(MovingSphere& smallSphere, 
        const glm::vec3& hollowCenter, 
        float hollowRadius, 
        const glm::vec3& hollowLinearVel,
        const glm::vec3& hollowAngularVel,
        float restitution, 
        float friction) {
        glm::vec3 delta = smallSphere.sphere.Position() - hollowCenter;
        float dist = glm::length(delta);
        float maxAllowedDist = hollowRadius - smallSphere.sphere.m_radius;

        if (dist > maxAllowedDist && dist > 0.0001f) {
            glm::vec3 normal = -delta / dist; // Points towards center

            glm::vec3 contactPoint = hollowCenter + (delta / dist) * hollowRadius;
            
            // Calculate wall velocity at contact point
            glm::vec3 pointOffset = contactPoint - hollowCenter;
            glm::vec3 wallVel = hollowLinearVel + glm::cross(hollowAngularVel, pointOffset);

            Plane p(contactPoint, normal);
            ResolveSpherePlaneCollision(smallSphere, p, restitution, friction, wallVel);
            smallSphere.sphere.SetPosition(hollowCenter + (delta / dist) * maxAllowedDist);
        }
    }
}

void PhysicsSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();
         
    if (simulationPaused) return;

    static std::unordered_set<Entity> initializedEntities;

    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();

    const size_t physCount = physicsArray->GetSize();
    for (size_t idx = 0; idx < physCount; ++idx) {
        Entity i = physicsArray->GetEntityAtIndex(idx);
        if (i != MAX_ENTITIES && transformArray->HasData(i)) {
            // If we haven't initialized this entity's physics rotation yet...
            if (initializedEntities.find(i) == initializedEntities.end()) {
                auto& transform = transformArray->GetData(i);
                auto& physics = physicsArray->GetData(i);

                // Sync the physics orientation to the visual starting rotation
                glm::quat q = glm::quat(glm::radians(transform.rotation));
                physics.orientation = glm::mat3_cast(q);

                initializedEntities.insert(i);
            }
        }
    }
    // ==========================================

    // --- NEW: AUTO-SYNC COLLIDERS TO VISUAL SCALE ---
    auto colliderArray = registry.GetComponentArray<ColliderComponent>();

    const size_t colliderCount = colliderArray->GetSize();
    for (size_t idx = 0; idx < colliderCount; ++idx) {
        Entity i = colliderArray->GetEntityAtIndex(idx);
        if (i != MAX_ENTITIES && transformArray->HasData(i)) {
            auto& transform = transformArray->GetData(i);
            auto& collider = colliderArray->GetData(i);

            if (collider.autoScale) {
                // Extract absolute scales just in case of negative scaling
                const float scaleX = std::abs(transform.scale.x);
                const float scaleY = std::abs(transform.scale.y);
                const float scaleZ = std::abs(transform.scale.z);
                const float maxScale = std::max({ scaleX, scaleY, scaleZ });

                // Sync based on collider type
                if (collider.type == 0) { // Sphere
                    // If base geometry is diameter 1.0 (radius 0.5):
                    collider.radius = maxScale * 0.5f; 
                }
                else if (collider.type == 2) { // Capsule / Cylinder
                    // Assuming X/Z scaling changes thickness, and Y changes height
                    collider.radius = std::max(scaleX, scaleZ) * 0.5f;
                    collider.height = scaleY; 
                }
                else if (collider.type == 3) { // Box / AABB
                    collider.halfExtents = glm::vec3(scaleX, scaleY, scaleZ) * 0.5f;
                    collider.radius = std::max({ scaleX, scaleY, scaleZ }) * 0.5f;
                    if (physicsArray->HasData(i)) {
                        auto& phys = physicsArray->GetData(i);
                        if (!phys.isStatic) phys.SetBoxInertia(collider.halfExtents);
                    }
                }
            }
        }
    }
    // ------------------------------------------------

    const float dt = deltaTime / static_cast<float>(subSteps);

    auto springArray = registry.GetComponentArray<SpringComponent>();

    std::unordered_set<Entity> pendingDelete;

    // Optimization: Build active lists once per Update instead of per sub-step
    std::vector<Entity> activeColliders;
    std::vector<Entity> activeSpheres;
    activeColliders.reserve(colliderCount);
    activeSpheres.reserve(colliderCount);

    for (size_t idx = 0; idx < colliderCount; ++idx) {
        Entity i = colliderArray->GetEntityAtIndex(idx);
        if (i != MAX_ENTITIES && transformArray->HasData(i) && physicsArray->HasData(i)) {
            const auto& col = colliderArray->GetData(i);
            if (col.hasCollision && !col.isClothParticle) {
                activeColliders.push_back(i);
                if (col.type == 0) { // 0 = Sphere
                    activeSpheres.push_back(i);
                }
            }
        }
    }

    // 1. Move the sub-step loop to wrap ALL physics force applications
    for (int i = 0; i < subSteps; ++i) {
        
        // --- Spring forces: iterate entities with SpringComponent ---
        const size_t springCount = springArray->GetSize();
        for (size_t idx = 0; idx < springCount; ++idx) {
            Entity e = springArray->GetEntityAtIndex(idx);
            if (e == MAX_ENTITIES || !transformArray->HasData(e) || !physicsArray->HasData(e)) {
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
                if (target == MAX_ENTITIES || !registry.IsAlive(target)) continue;
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
        ResolveCollisions(scene, registry, dt, activeColliders, activeSpheres, pendingDelete);
    }

    for (Entity e : pendingDelete) {
        if (scene.IsLookaheadMode()) {
            scene.DeactivateEntityForLookahead(e);
        } else {
            scene.DeleteEntity(e);
        }
    }

    // 3. Update transforms once per frame after all sub-steps
    const size_t physicsCountFinal = physicsArray->GetSize();
    for (size_t iIdx = 0; iIdx < physicsCountFinal; ++iIdx) {
        Entity i = physicsArray->GetEntityAtIndex(iIdx);
        if (i != MAX_ENTITIES && transformArray->HasData(i)) {
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

    // Periodic ownership distribution logging (every 60 frames)
    if (++s_physicsFrameCount % 60 == 0) {
        int localCount = 0;
        int remoteCount = 0;
        int staticCount = 0;
        auto ownershipArray = registry.GetComponentArray<OwnershipComponent>();
        auto physicsArrayFinal = registry.GetComponentArray<PhysicsComponent>();
        
        const size_t ownershipCount = ownershipArray->GetSize();
        for (size_t eIdx = 0; eIdx < ownershipCount; ++eIdx) {
            Entity e = ownershipArray->GetEntityAtIndex(eIdx);
            if (e == MAX_ENTITIES) continue;

            if (physicsArrayFinal->HasData(e) && physicsArrayFinal->GetData(e).isStatic) {
                staticCount++;
            } else {
                if (static_cast<int>(ownershipArray->GetData(e).GetOwnerIndex()) == localPeerId) {
                    localCount++;
                } else {
                    remoteCount++;
                }
            }
        }

        // Also count statics that might not have ownership components
        const size_t physicsCountAll = physicsArrayFinal->GetSize();
        for (size_t pIdx = 0; pIdx < physicsCountAll; ++pIdx) {
            Entity p = physicsArrayFinal->GetEntityAtIndex(pIdx);
            if (p != MAX_ENTITIES && physicsArrayFinal->GetData(p).isStatic && !ownershipArray->HasData(p)) {
                staticCount++;
            }
        }
        // std::cout << "[PhysicsSystem] Frame " << s_physicsFrameCount << " Distribution: Local=" << localCount 
        //           << " Remote=" << remoteCount << " Static=" << staticCount 
        //           << " (LocalPeerID: " << localPeerId << ")" << std::endl;
    }
}

void PhysicsSystem::Integrate(Registry& registry, float dt) {
    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();
    auto colliderArray = registry.GetComponentArray<ColliderComponent>();
    auto ownershipArray = registry.GetComponentArray<OwnershipComponent>();

    const size_t physicsCount = physicsArray->GetSize();
    for (size_t idx = 0; idx < physicsCount; ++idx) {
        Entity i = physicsArray->GetEntityAtIndex(idx);
        if (i == MAX_ENTITIES || !transformArray->HasData(i)) continue;

        if (ownershipArray->HasData(i)) {
            auto& ownership = ownershipArray->GetData(i);
            // Only skip if we have a valid peer ID AND we are not the owner.
            // If localPeerId is -1, we are in standalone mode or joining, so we simulate everything.
            if (localPeerId != -1 && static_cast<int>(ownership.GetOwnerIndex()) != localPeerId) {
                continue;
            }
        }

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
                    // For a true capsule, total height H includes the spherical caps.
                    // The distance between the sphere centers is H - 2r.
                    float halfSegmentLen = std::max(0.0f, (colHeight * 0.5f) - colRadius);
                    glm::vec3 p1 = transform.position - up * halfSegmentLen;
                    glm::vec3 p2 = transform.position + up * halfSegmentLen;
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

                // Safety: Clamp extreme velocities and check for NaNs to prevent "freezing" or disappearing objects
                if (glm::any(glm::isnan(physics.velocity)) || glm::length(physics.velocity) > 1000.0f) {
                    physics.velocity = glm::vec3(0.0f);
                }
                if (glm::any(glm::isnan(physics.angularVelocity)) || glm::length(physics.angularVelocity) > 1000.0f) {
                    physics.angularVelocity = glm::vec3(0.0f);
                }
                if (glm::any(glm::isnan(transform.position))) {
                    transform.position = glm::vec3(0.0f, 10.0f, 0.0f); // Safe recovery position
                }

                physics.forceAccumulator = glm::vec3(0.0f);
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

void PhysicsSystem::ResolveCollisions(Scene& scene, Registry& registry, float dt, const std::vector<Entity>& activeColliders, const std::vector<Entity>& activeSpheres, std::unordered_set<Entity>& pendingDelete) {
    bool useForce = (currentResolutionMethod == ResolutionMethod::Force);

    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto colliderArray = registry.GetComponentArray<ColliderComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();
    auto despawnerArray = registry.GetComponentArray<DespawnerComponent>();
    auto ownershipArray = registry.GetComponentArray<OwnershipComponent>();

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

    // Use the pre-filtered active lists passed from Update()
    const size_t activeCount = activeColliders.size();
    for (size_t iIdx = 0; iIdx < activeCount; ++iIdx) {
        Entity i = activeColliders[iIdx];
        if (!registry.IsAlive(i)) continue;

        for (size_t jIdx = iIdx + 1; jIdx < activeCount; ++jIdx) {
            Entity j = activeColliders[jIdx];
            if (!registry.IsAlive(j)) continue;

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
                    bool ownA = true;
                    if (ownershipArray->HasData(i)) {
                        ownA = (static_cast<int>(ownershipArray->GetData(i).GetOwnerIndex()) == localPeerId);
                    }

                    bool ownB = true;
                    if (ownershipArray->HasData(j)) {
                        ownB = (static_cast<int>(ownershipArray->GetData(j).GetOwnerIndex()) == localPeerId);
                    }

                    if (!ownA && !ownB) continue;

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
            // Sphere vs Sphere (Inside Check)
            else if (c1.type == 0 && c2.type == 0 && c2.collisionSide == CollisionSide::INSIDE) {
                MovingSphere sphereA(t1.position, c1.radius, p1.velocity, p1.inverseMass, p1.restitution);
                sphereA.angularVelocity = p1.angularVelocity;
                sphereA.inertiaTensor = p1.inertiaTensor;
                sphereA.inverseInertiaTensor = p1.inverseInertiaTensor;

                ResolveSphereInsideSphere(sphereA, t2.position, c2.radius, p2.velocity, p2.angularVelocity, p2.restitution, p1.friction);

                if (!p1.isStatic) {
                    p1.velocity = sphereA.velocity;
                    p1.angularVelocity = sphereA.angularVelocity;
                    t1.position = sphereA.sphere.Position();
                    t1.UpdateMatrix();
                }
            }
            // Sphere vs Sphere (Inside Check - Symmetric)
            else if (c1.type == 0 && c2.type == 0 && c1.collisionSide == CollisionSide::INSIDE) {
                MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.inverseMass, p2.restitution);
                sphereB.angularVelocity = p2.angularVelocity;
                sphereB.inertiaTensor = p2.inertiaTensor;
                sphereB.inverseInertiaTensor = p2.inverseInertiaTensor;

                ResolveSphereInsideSphere(sphereB, t1.position, c1.radius, p1.velocity, p1.angularVelocity, p1.restitution, p2.friction);

                if (!p2.isStatic) {
                    p2.velocity = sphereB.velocity;
                    p2.angularVelocity = sphereB.angularVelocity;
                    t2.position = sphereB.sphere.Position();
                    t2.UpdateMatrix();
                }
            }
            // Cylinder/Capsule vs Sphere (Inside Check)
            else if (c1.type == 2 && c2.type == 0 && c1.collisionSide == CollisionSide::INSIDE) {
                MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.inverseMass, p2.restitution);
                sphereB.angularVelocity = p2.angularVelocity;
                sphereB.inertiaTensor = p2.inertiaTensor;
                sphereB.inverseInertiaTensor = p2.inverseInertiaTensor;

                ResolveSphereInsideCapsule(sphereB, t1.position, c1.radius, c1.height, p1.orientation, p1.velocity, p1.angularVelocity, p1.restitution, p2.friction);

                if (!p2.isStatic) {
                    p2.velocity = sphereB.velocity;
                    p2.angularVelocity = sphereB.angularVelocity;
                    t2.position = sphereB.sphere.Position();
                    t2.UpdateMatrix();
                }
            }
            // Sphere vs Cylinder/Capsule (Inside Check)
            else if (c1.type == 0 && c2.type == 2 && c2.collisionSide == CollisionSide::INSIDE) {
                MovingSphere sphereA(t1.position, c1.radius, p1.velocity, p1.inverseMass, p1.restitution);
                sphereA.angularVelocity = p1.angularVelocity;
                sphereA.inertiaTensor = p1.inertiaTensor;
                sphereA.inverseInertiaTensor = p1.inverseInertiaTensor;

                ResolveSphereInsideCapsule(sphereA, t2.position, c2.radius, c2.height, p2.orientation, p2.velocity, p2.angularVelocity, p2.restitution, p1.friction);

                if (!p1.isStatic) {
                    p1.velocity = sphereA.velocity;
                    p1.angularVelocity = sphereA.angularVelocity;
                    t1.position = sphereA.sphere.Position();
                    t1.UpdateMatrix();
                }
            }
            // Sphere vs Box
            else if (c1.type == 0 && c2.type == 3) {
                MovingSphere sphereA(t1.position, c1.radius, p1.velocity, p1.inverseMass, p1.restitution);
                sphereA.angularVelocity = p1.angularVelocity;
                sphereA.inertiaTensor = p1.inertiaTensor;
                sphereA.inverseInertiaTensor = p1.inverseInertiaTensor;

                if (c2.collisionSide == CollisionSide::INSIDE) {
                    ResolveSphereInsideAABB(
                        sphereA,
                        t2.position,
                        c2.halfExtents,
                        p2.orientation,
                        p2.velocity,
                        p2.angularVelocity,
                        p2.restitution,
                        p1.friction);
                    if (!p1.isStatic) {
                        p1.velocity = sphereA.velocity;
                        p1.angularVelocity = sphereA.angularVelocity;
                        t1.position = sphereA.sphere.Position();
                        t1.UpdateMatrix();
                    }
                } else {
                    // Sphere vs Box OUTSIDE: clamp sphere center to box to find closest point
                    glm::vec3 boxMin = t2.position - c2.halfExtents;
                    glm::vec3 boxMax = t2.position + c2.halfExtents;
                    glm::vec3 closest = glm::clamp(t1.position, boxMin, boxMax);
                    glm::vec3 diff = t1.position - closest;
                    float distSq = glm::dot(diff, diff);
                    if (distSq > 1e-10f && distSq < c1.radius * c1.radius) {
                        float dist = std::sqrt(distSq);
                        glm::vec3 n = diff / dist;
                        float penetration = c1.radius - dist;

                        glm::vec3 r1 = closest - t1.position; // sphere CoM to contact
                        glm::vec3 r2 = closest - t2.position; // box CoM to contact

                        glm::vec3 v1c = p1.velocity + glm::cross(p1.angularVelocity, r1);
                        glm::vec3 v2c = p2.velocity + glm::cross(p2.angularVelocity, r2);
                        float velAlongNormal = glm::dot(v1c - v2c, n);

                        if (velAlongNormal < 0.0f) {
                            const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);
                            float e = std::min(p1.restitution, p2.restitution);
                            float totalInvMass = p1.inverseMass + p2.inverseMass;

                            glm::vec3 r1CrossN = glm::cross(r1, n);
                            glm::vec3 r2CrossN = glm::cross(r2, n);
                            float angDenom1 = glm::dot(n, glm::cross(p1.inverseInertiaTensor * r1CrossN, r1));
                            float angDenom2 = glm::dot(n, glm::cross(p2.inverseInertiaTensor * r2CrossN, r2));
                            float denom = std::max(totalInvMass + angDenom1 + angDenom2, 1e-6f);

                            float j = -(1.0f + e) * velAlongNormal / denom;
                            glm::vec3 impulse = j * n;

                            if (!p1.isStatic) {
                                p1.velocity += p1.inverseMass * impulse;
                                p1.angularVelocity += p1.inverseInertiaTensor * glm::cross(r1, impulse);
                            }
                            if (!p2.isStatic) {
                                p2.velocity -= p2.inverseMass * impulse;
                                p2.angularVelocity -= p2.inverseInertiaTensor * glm::cross(r2, impulse);
                            }

                            // Friction
                            glm::vec3 postV1c = p1.velocity + glm::cross(p1.angularVelocity, r1);
                            glm::vec3 postV2c = p2.velocity + glm::cross(p2.angularVelocity, r2);
                            glm::vec3 tangVel = (postV1c - postV2c) - glm::dot(postV1c - postV2c, n) * n;
                            float tangSpeed = glm::length(tangVel);
                            if (tangSpeed > 1e-6f) {
                                glm::vec3 tangDir = tangVel / tangSpeed;
                                glm::vec3 r1CrossT = glm::cross(r1, tangDir);
                                glm::vec3 r2CrossT = glm::cross(r2, tangDir);
                                float tDenom = std::max(totalInvMass +
                                    glm::dot(tangDir, glm::cross(p1.inverseInertiaTensor * r1CrossT, r1)) +
                                    glm::dot(tangDir, glm::cross(p2.inverseInertiaTensor * r2CrossT, r2)), 1e-6f);
                                float jf = std::min(contactFriction * std::abs(j), tangSpeed / tDenom);
                                glm::vec3 fi = -jf * tangDir;
                                if (!p1.isStatic) {
                                    p1.velocity += p1.inverseMass * fi;
                                    p1.angularVelocity += p1.inverseInertiaTensor * glm::cross(r1, fi);
                                }
                                if (!p2.isStatic) {
                                    p2.velocity -= p2.inverseMass * fi;
                                    p2.angularVelocity -= p2.inverseInertiaTensor * glm::cross(r2, fi);
                                }
                            }

                            // Positional correction
                            float corrMag = penetration / std::max(totalInvMass, 1e-6f) * 0.2f;
                            if (!p1.isStatic) { t1.position += n * corrMag * p1.inverseMass; t1.UpdateMatrix(); }
                            if (!p2.isStatic) { t2.position -= n * corrMag * p2.inverseMass; t2.UpdateMatrix(); }
                        }
                    }
                }
            }
            // Box vs Sphere
            else if (c1.type == 3 && c2.type == 0) {
                if (c1.collisionSide == CollisionSide::INSIDE) {
                    MovingSphere sphereB(t2.position, c2.radius, p2.velocity, p2.inverseMass, p2.restitution);
                    sphereB.angularVelocity = p2.angularVelocity;
                    sphereB.inertiaTensor = p2.inertiaTensor;
                    sphereB.inverseInertiaTensor = p2.inverseInertiaTensor;

                    ResolveSphereInsideAABB(
                        sphereB,
                        t1.position,
                        c1.halfExtents,
                        p1.orientation,
                        p1.velocity,
                        p1.angularVelocity,
                        p1.restitution,
                        p2.friction);
                    if (!p2.isStatic) {
                        p2.velocity = sphereB.velocity;
                        p2.angularVelocity = sphereB.angularVelocity;
                        t2.position = sphereB.sphere.Position();
                        t2.UpdateMatrix();
                    }
                } else {
                    // Box vs Sphere OUTSIDE: clamp sphere center to box
                    glm::vec3 boxMin = t1.position - c1.halfExtents;
                    glm::vec3 boxMax = t1.position + c1.halfExtents;
                    glm::vec3 closest = glm::clamp(t2.position, boxMin, boxMax);
                    glm::vec3 diff = t2.position - closest;
                    float distSq = glm::dot(diff, diff);
                    if (distSq > 1e-10f && distSq < c2.radius * c2.radius) {
                        float dist = std::sqrt(distSq);
                        glm::vec3 n = diff / dist;
                        float penetration = c2.radius - dist;

                        glm::vec3 r1 = closest - t1.position; // box CoM to contact
                        glm::vec3 r2 = closest - t2.position; // sphere CoM to contact

                        glm::vec3 v1c = p1.velocity + glm::cross(p1.angularVelocity, r1);
                        glm::vec3 v2c = p2.velocity + glm::cross(p2.angularVelocity, r2);
                        float velAlongNormal = glm::dot(v2c - v1c, n);

                        if (velAlongNormal < 0.0f) {
                            const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);
                            float e = std::min(p1.restitution, p2.restitution);
                            float totalInvMass = p1.inverseMass + p2.inverseMass;

                            glm::vec3 r1CrossN = glm::cross(r1, n);
                            glm::vec3 r2CrossN = glm::cross(r2, n);
                            float angDenom1 = glm::dot(n, glm::cross(p1.inverseInertiaTensor * r1CrossN, r1));
                            float angDenom2 = glm::dot(n, glm::cross(p2.inverseInertiaTensor * r2CrossN, r2));
                            float denom = std::max(totalInvMass + angDenom1 + angDenom2, 1e-6f);

                            float j = -(1.0f + e) * velAlongNormal / denom;
                            glm::vec3 impulse = j * n;

                            if (!p1.isStatic) {
                                p1.velocity -= p1.inverseMass * impulse;
                                p1.angularVelocity -= p1.inverseInertiaTensor * glm::cross(r1, impulse);
                            }
                            if (!p2.isStatic) {
                                p2.velocity += p2.inverseMass * impulse;
                                p2.angularVelocity += p2.inverseInertiaTensor * glm::cross(r2, impulse);
                            }

                            // Friction
                            glm::vec3 postV1c = p1.velocity + glm::cross(p1.angularVelocity, r1);
                            glm::vec3 postV2c = p2.velocity + glm::cross(p2.angularVelocity, r2);
                            glm::vec3 tangVel = (postV2c - postV1c) - glm::dot(postV2c - postV1c, n) * n;
                            float tangSpeed = glm::length(tangVel);
                            if (tangSpeed > 1e-6f) {
                                glm::vec3 tangDir = tangVel / tangSpeed;
                                glm::vec3 r1CrossT = glm::cross(r1, tangDir);
                                glm::vec3 r2CrossT = glm::cross(r2, tangDir);
                                float tDenom = std::max(totalInvMass +
                                    glm::dot(tangDir, glm::cross(p1.inverseInertiaTensor * r1CrossT, r1)) +
                                    glm::dot(tangDir, glm::cross(p2.inverseInertiaTensor * r2CrossT, r2)), 1e-6f);
                                float jf = std::min(contactFriction * std::abs(j), tangSpeed / tDenom);
                                glm::vec3 fi = jf * tangDir;
                                if (!p1.isStatic) {
                                    p1.velocity -= p1.inverseMass * fi;
                                    p1.angularVelocity -= p1.inverseInertiaTensor * glm::cross(r1, fi);
                                }
                                if (!p2.isStatic) {
                                    p2.velocity += p2.inverseMass * fi;
                                    p2.angularVelocity += p2.inverseInertiaTensor * glm::cross(r2, fi);
                                }
                            }

                            // Positional correction
                            float corrMag = penetration / std::max(totalInvMass, 1e-6f) * 0.2f;
                            if (!p1.isStatic) { t1.position -= n * corrMag * p1.inverseMass; t1.UpdateMatrix(); }
                            if (!p2.isStatic) { t2.position += n * corrMag * p2.inverseMass; t2.UpdateMatrix(); }
                        }
                    }
                }
            }
            // Box vs Box
            else if (c1.type == 3 && c2.type == 3 &&
                     c1.collisionSide != CollisionSide::INSIDE &&
                     c2.collisionSide != CollisionSide::INSIDE) {
                AABB aabbA(t1.position, c1.halfExtents);
                MovingBox boxA(aabbA, p1.velocity, p1.inverseMass, p1.restitution);
                boxA.angularVelocity = p1.angularVelocity;
                boxA.inverseInertiaTensor = p1.inverseInertiaTensor;
                boxA.orientation = p1.orientation; // <-- FIX: Assign Orientation

                AABB aabbB(t2.position, c2.halfExtents);
                MovingBox boxB(aabbB, p2.velocity, p2.inverseMass, p2.restitution);
                boxB.angularVelocity = p2.angularVelocity;
                boxB.inverseInertiaTensor = p2.inverseInertiaTensor;
                boxB.orientation = p2.orientation; // <-- FIX: Assign Orientation

                ResolveBoxBoxCollision(boxA, boxB);

                if (!p1.isStatic) {
                    p1.velocity = boxA.velocity;
                    p1.angularVelocity = boxA.angularVelocity;
                    t1.position = boxA.box.Position();
                    t1.UpdateMatrix();
                }
                if (!p2.isStatic) {
                    p2.velocity = boxB.velocity;
                    p2.angularVelocity = boxB.angularVelocity;
                    t2.position = boxB.box.Position();
                    t2.UpdateMatrix();
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
                float segmentHalfLength = std::max(0.0f, (c1.height - 2.0f * c1.radius) * 0.5f);
                glm::vec3 p1_local = t1.position - up * segmentHalfLength;
                glm::vec3 p2_local = t1.position + up * segmentHalfLength;
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
                        // APPLY REPLACEMENT HERE:
                        p1.velocity = capsuleA.velocity;
                        p1.angularVelocity = capsuleA.angularVelocity;

                        float d1 = planeB.GetSignedDistance(p1_local);
                        float d2 = planeB.GetSignedDistance(p2_local);
                        float deepestDist = std::min(d1, d2);
                        float overlap = c1.radius - deepestDist;

                        if (overlap > 0.0f) {
                            const float correctionPercent = 0.5f;
                            t1.position += planeB.GetNormal() * (overlap * correctionPercent);
                            t1.UpdateMatrix();
                        }
                        ApplySleepThreshold(p1, planeB);
                    }
                }
            }
            // Plane vs Capsule
            else if (c1.type == 1 && c2.type == 2) {
                glm::vec3 up = p2.orientation * glm::vec3(0, 1, 0);
                float segmentHalfLength = std::max(0.0f, (c2.height - 2.0f * c2.radius) * 0.5f);
                glm::vec3 p1_local = t2.position - up * segmentHalfLength;
                glm::vec3 p2_local = t2.position + up * segmentHalfLength;
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
                        // APPLY REPLACEMENT HERE (Adjusted for entity 2):
                        p2.velocity = capsuleB.velocity;
                        p2.angularVelocity = capsuleB.angularVelocity;

                        float d1 = planeA.GetSignedDistance(p1_local); // p1_local/p2_local are defined earlier in this block
                        float d2 = planeA.GetSignedDistance(p2_local);
                        float deepestDist = std::min(d1, d2);
                        float overlap = c2.radius - deepestDist;

                        if (overlap > 0.0f) {
                            const float correctionPercent = 0.5f;
                            t2.position += planeA.GetNormal() * (overlap * correctionPercent);
                            t2.UpdateMatrix();
                        }
                        ApplySleepThreshold(p2, planeA);
                    }
                }
            }
            // Box vs Plane
            else if (c1.type == 3 && c2.type == 1 &&
                c1.collisionSide != CollisionSide::INSIDE) {
                Plane planeB(t2.position, c2.normal, c2.radius);
                glm::vec3 n = planeB.GetNormal();
                glm::vec3 h = c1.halfExtents;

                const glm::vec3 cornerSigns[8] = {
                    {-1,-1,-1},{-1,-1,+1},{-1,+1,-1},{-1,+1,+1},
                    {+1,-1,-1},{+1,-1,+1},{+1,+1,-1},{+1,+1,+1}
                };

                glm::vec3 contactSum(0.0f);
                int contactCount = 0;
                float maxPenetration = 0.0f;

                for (const auto& s : cornerSigns) {
                    // FIX: Rotate the corners by the box's orientation!
                    glm::vec3 corner = t1.position + p1.orientation * (h * s);
                    float d = planeB.GetSignedDistance(corner);
                    if (d < 0.0f) {
                        contactSum += corner;
                        ++contactCount;
                        maxPenetration = std::max(maxPenetration, -d);
                    }
                }

                if (contactCount > 0 &&
                    IsSphereInsideFinitePlaneBounds(t2, c2, t1.position, glm::length(h))) {

                    queueDespawnerDeletion(i, j, p1, p2);

                    glm::vec3 contactPoint = contactSum / float(contactCount);
                    glm::vec3 r = contactPoint - t1.position;
                    glm::vec3 contactVel = p1.velocity + glm::cross(p1.angularVelocity, r);
                    float velAlongNormal = glm::dot(contactVel, n);

                    if (!p1.isStatic && velAlongNormal < 0.0f) {
                        const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);
                        float e = std::min(p1.restitution, p2.restitution);

                        // FIX: Use World-Space Inverse Inertia Tensor
                        glm::mat3 invInertiaWorld = p1.orientation * p1.inverseInertiaTensor * glm::transpose(p1.orientation);

                        glm::vec3 rCrossN = glm::cross(r, n);
                        float angDenom = glm::dot(n, glm::cross(invInertiaWorld * rCrossN, r));
                        float denom = std::max(p1.inverseMass + angDenom, 1e-6f);

                        float j = -(1.0f + e) * velAlongNormal / denom;
                        glm::vec3 impulse = j * n;

                        p1.velocity += p1.inverseMass * impulse;
                        p1.angularVelocity += invInertiaWorld * glm::cross(r, impulse); // FIX

                        glm::vec3 postContactVel = p1.velocity + glm::cross(p1.angularVelocity, r);
                        glm::vec3 tangVel = postContactVel - glm::dot(postContactVel, n) * n;
                        float tangSpeed = glm::length(tangVel);

                        if (tangSpeed > 1e-6f) {
                            glm::vec3 tangDir = tangVel / tangSpeed;
                            glm::vec3 rCrossT = glm::cross(r, tangDir);
                            float tangDenom = std::max(p1.inverseMass + glm::dot(tangDir, glm::cross(invInertiaWorld * rCrossT, r)), 1e-6f); // FIX

                            float jf = std::min(contactFriction * std::abs(j), tangSpeed / tangDenom);
                            glm::vec3 frictionImpulse = -jf * tangDir;

                            p1.velocity += p1.inverseMass * frictionImpulse;
                            p1.angularVelocity += invInertiaWorld * glm::cross(r, frictionImpulse); // FIX
                        }
                    }
                    if (!p1.isStatic) {
                        t1.position += n * maxPenetration;
                        t1.UpdateMatrix();
                        ApplySleepThreshold(p1, planeB);
                    }
                }
                }
                // Plane vs Box
            else if (c1.type == 1 && c2.type == 3 &&
                c2.collisionSide != CollisionSide::INSIDE) {
                Plane planeA(t1.position, c1.normal, c1.radius);
                glm::vec3 n = planeA.GetNormal();
                glm::vec3 h = c2.halfExtents;

                const glm::vec3 cornerSigns[8] = {
                    {-1,-1,-1},{-1,-1,+1},{-1,+1,-1},{-1,+1,+1},
                    {+1,-1,-1},{+1,-1,+1},{+1,+1,-1},{+1,+1,+1}
                };

                glm::vec3 contactSum(0.0f);
                int contactCount = 0;
                float maxPenetration = 0.0f;

                for (const auto& s : cornerSigns) {
                    // FIX: Rotate the corners by the box's orientation!
                    glm::vec3 corner = t2.position + p2.orientation * (h * s);
                    float d = planeA.GetSignedDistance(corner);
                    if (d < 0.0f) {
                        contactSum += corner;
                        ++contactCount;
                        maxPenetration = std::max(maxPenetration, -d);
                    }
                }

                if (contactCount > 0 &&
                    IsSphereInsideFinitePlaneBounds(t1, c1, t2.position, glm::length(h))) {

                    queueDespawnerDeletion(i, j, p1, p2);

                    glm::vec3 contactPoint = contactSum / float(contactCount);
                    glm::vec3 r = contactPoint - t2.position;
                    glm::vec3 contactVel = p2.velocity + glm::cross(p2.angularVelocity, r);
                    float velAlongNormal = glm::dot(contactVel, n);

                    if (!p2.isStatic && velAlongNormal < 0.0f) {
                        const float contactFriction = ComputeContactFriction(p1.friction, p2.friction, PhysicsSystem::contactFrictionScale);
                        float e = std::min(p1.restitution, p2.restitution);

                        // FIX: Use World-Space Inverse Inertia Tensor
                        glm::mat3 invInertiaWorld = p2.orientation * p2.inverseInertiaTensor * glm::transpose(p2.orientation);

                        glm::vec3 rCrossN = glm::cross(r, n);
                        float angDenom = glm::dot(n, glm::cross(invInertiaWorld * rCrossN, r));
                        float denom = std::max(p2.inverseMass + angDenom, 1e-6f);

                        float j = -(1.0f + e) * velAlongNormal / denom;
                        glm::vec3 impulse = j * n;

                        p2.velocity += p2.inverseMass * impulse;
                        p2.angularVelocity += invInertiaWorld * glm::cross(r, impulse); // FIX

                        glm::vec3 postContactVel = p2.velocity + glm::cross(p2.angularVelocity, r);
                        glm::vec3 tangVel = postContactVel - glm::dot(postContactVel, n) * n;
                        float tangSpeed = glm::length(tangVel);

                        if (tangSpeed > 1e-6f) {
                            glm::vec3 tangDir = tangVel / tangSpeed;
                            glm::vec3 rCrossT = glm::cross(r, tangDir);
                            float tangDenom = std::max(p2.inverseMass + glm::dot(tangDir, glm::cross(invInertiaWorld * rCrossT, r)), 1e-6f); // FIX

                            float jf = std::min(contactFriction * std::abs(j), tangSpeed / tangDenom);
                            glm::vec3 frictionImpulse = -jf * tangDir;

                            p2.velocity += p2.inverseMass * frictionImpulse;
                            p2.angularVelocity += invInertiaWorld * glm::cross(r, frictionImpulse); // FIX
                        }
                    }
                    if (!p2.isStatic) {
                        t2.position += n * maxPenetration;
                        t2.UpdateMatrix();
                        ApplySleepThreshold(p2, planeA);
                    }
                }
            }
            // Capsule vs Capsule
            else if (c1.type == 2 && c2.type == 2) {
                glm::vec3 upA = p1.orientation * glm::vec3(0, 1, 0);
                float halfLenA = std::max(0.0f, (c1.height - 2.0f * c1.radius) * 0.5f);
                glm::vec3 p1A = t1.position - upA * halfLenA;
                glm::vec3 p2A = t1.position + upA * halfLenA;
                Capsule capA(p1A, p2A, c1.radius);
                MovingCapsule mCapA(capA, p1.velocity, p1.inverseMass, p1.restitution);
                mCapA.angularVelocity = p1.angularVelocity;
                mCapA.inverseInertiaTensor = p1.inverseInertiaTensor;
                mCapA.orientation = p1.orientation;

                glm::vec3 upB = p2.orientation * glm::vec3(0, 1, 0);
                float halfLenB = std::max(0.0f, (c2.height - 2.0f * c2.radius) * 0.5f);
                glm::vec3 p1B = t2.position - upB * halfLenB;
                glm::vec3 p2B = t2.position + upB * halfLenB;
                Capsule capB(p1B, p2B, c2.radius);
                MovingCapsule mCapB(capB, p2.velocity, p2.inverseMass, p2.restitution);
                mCapB.angularVelocity = p2.angularVelocity;
                mCapB.inverseInertiaTensor = p2.inverseInertiaTensor;
                mCapB.orientation = p2.orientation;

                if (ResolveCapsuleCapsuleCollision(mCapA, mCapB)) {
                    queueDespawnerDeletion(i, j, p1, p2);
                }
                if (!p1.isStatic) {
                    p1.velocity = mCapA.velocity;
                    p1.angularVelocity = mCapA.angularVelocity;
                    t1.position = (mCapA.capsule.Position() + mCapA.capsule.m_p2) * 0.5f;
                    t1.UpdateMatrix();
                }
                if (!p2.isStatic) {
                    p2.velocity = mCapB.velocity;
                    p2.angularVelocity = mCapB.angularVelocity;
                    t2.position = (mCapB.capsule.Position() + mCapB.capsule.m_p2) * 0.5f;
                    t2.UpdateMatrix();
                }
                }
                // Box vs Capsule
            else if (c1.type == 3 && c2.type == 2 && c1.collisionSide != CollisionSide::INSIDE) {
                    AABB aabbA(t1.position, c1.halfExtents);
                    MovingBox boxA(aabbA, p1.velocity, p1.inverseMass, p1.restitution);
                    boxA.angularVelocity = p1.angularVelocity;
                    boxA.inverseInertiaTensor = p1.inverseInertiaTensor;
                    boxA.orientation = p1.orientation;

                    glm::vec3 upB = p2.orientation * glm::vec3(0, 1, 0);
                    float halfLenB = std::max(0.0f, (c2.height - 2.0f * c2.radius) * 0.5f);
                    glm::vec3 p1B = t2.position - upB * halfLenB;
                    glm::vec3 p2B = t2.position + upB * halfLenB;
                    Capsule capB(p1B, p2B, c2.radius);
                    MovingCapsule mCapB(capB, p2.velocity, p2.inverseMass, p2.restitution);
                    mCapB.angularVelocity = p2.angularVelocity;
                    mCapB.inverseInertiaTensor = p2.inverseInertiaTensor;
                    mCapB.orientation = p2.orientation;

                    if (ResolveBoxCapsuleCollision(boxA, mCapB)) {
                        queueDespawnerDeletion(i, j, p1, p2); 
                    }
                    if (!p1.isStatic) {
                        p1.velocity = boxA.velocity;
                        p1.angularVelocity = boxA.angularVelocity;
                        t1.position = boxA.box.Position();
                        t1.UpdateMatrix();
                    }
                    if (!p2.isStatic) {
                        p2.velocity = mCapB.velocity;
                        p2.angularVelocity = mCapB.angularVelocity;
                        t2.position = (mCapB.capsule.Position() + mCapB.capsule.m_p2) * 0.5f;
                        t2.UpdateMatrix();
                    }
                }
                    // Capsule vs Box
            else if (c1.type == 2 && c2.type == 3 && c2.collisionSide != CollisionSide::INSIDE) {
                        glm::vec3 upA = p1.orientation * glm::vec3(0, 1, 0);
                        float halfLenA = std::max(0.0f, (c1.height - 2.0f * c1.radius) * 0.5f);
                        glm::vec3 p1A = t1.position - upA * halfLenA;
                        glm::vec3 p2A = t1.position + upA * halfLenA;
                        Capsule capA(p1A, p2A, c1.radius);
                        MovingCapsule mCapA(capA, p1.velocity, p1.inverseMass, p1.restitution);
                        mCapA.angularVelocity = p1.angularVelocity;
                        mCapA.inverseInertiaTensor = p1.inverseInertiaTensor;
                        mCapA.orientation = p1.orientation;

                        AABB aabbB(t2.position, c2.halfExtents);
                        MovingBox boxB(aabbB, p2.velocity, p2.inverseMass, p2.restitution);
                        boxB.angularVelocity = p2.angularVelocity;
                        boxB.inverseInertiaTensor = p2.inverseInertiaTensor;
                        boxB.orientation = p2.orientation;

                        if (ResolveBoxCapsuleCollision(boxB, mCapA)) {
                            queueDespawnerDeletion(i, j, p1, p2);
                        }
                        if (!p1.isStatic) {
                            p1.velocity = mCapA.velocity;
                            p1.angularVelocity = mCapA.angularVelocity;
                            t1.position = (mCapA.capsule.Position() + mCapA.capsule.m_p2) * 0.5f;
                            t1.UpdateMatrix();
                        }
                        if (!p2.isStatic) {
                            p2.velocity = boxB.velocity;
                            p2.angularVelocity = boxB.angularVelocity;
                            t2.position = boxB.box.Position();
                            t2.UpdateMatrix();
                        }
                    }
                    // Sphere vs Capsule (OUTSIDE)
            else if (c1.type == 0 && c2.type == 2 && c2.collisionSide != CollisionSide::INSIDE) {
                MovingSphere mSphere(t1.position, c1.radius, p1.velocity, p1.inverseMass, p1.restitution);
                mSphere.angularVelocity = p1.angularVelocity;
                mSphere.inverseInertiaTensor = p1.inverseInertiaTensor;
                mSphere.orientation = p1.orientation;

                glm::vec3 upB = p2.orientation * glm::vec3(0, 1, 0);
                float halfLenB = std::max(0.0f, (c2.height - 2.0f * c2.radius) * 0.5f);
                glm::vec3 p1B = t2.position - upB * halfLenB;
                glm::vec3 p2B = t2.position + upB * halfLenB;
                Capsule capB(p1B, p2B, c2.radius);
                MovingCapsule mCapB(capB, p2.velocity, p2.inverseMass, p2.restitution);
                mCapB.angularVelocity = p2.angularVelocity;
                mCapB.inverseInertiaTensor = p2.inverseInertiaTensor;
                mCapB.orientation = p2.orientation;

                queueDespawnerDeletion(i, j, p1, p2);
                ResolveSphereCapsuleCollision(mSphere, mCapB);

                if (!p1.isStatic) {
                    p1.velocity = mSphere.velocity;
                    p1.angularVelocity = mSphere.angularVelocity;
                    t1.position = mSphere.sphere.Position();
                    t1.UpdateMatrix();
                }
                if (!p2.isStatic) {
                    p2.velocity = mCapB.velocity;
                    p2.angularVelocity = mCapB.angularVelocity;
                    t2.position = (mCapB.capsule.Position() + mCapB.capsule.m_p2) * 0.5f;
                    t2.UpdateMatrix();
                }
            }
            // Capsule vs Sphere (OUTSIDE)
            else if (c1.type == 2 && c2.type == 0 && c1.collisionSide != CollisionSide::INSIDE) {
                glm::vec3 upA = p1.orientation * glm::vec3(0, 1, 0);
                float halfLenA = std::max(0.0f, (c1.height - 2.0f * c1.radius) * 0.5f);
                glm::vec3 p1A = t1.position - upA * halfLenA;
                glm::vec3 p2A = t1.position + upA * halfLenA;
                Capsule capA(p1A, p2A, c1.radius);
                MovingCapsule mCapA(capA, p1.velocity, p1.inverseMass, p1.restitution);
                mCapA.angularVelocity = p1.angularVelocity;
                mCapA.inverseInertiaTensor = p1.inverseInertiaTensor;
                mCapA.orientation = p1.orientation;

                MovingSphere mSphere(t2.position, c2.radius, p2.velocity, p2.inverseMass, p2.restitution);
                mSphere.angularVelocity = p2.angularVelocity;
                mSphere.inverseInertiaTensor = p2.inverseInertiaTensor;
                mSphere.orientation = p2.orientation;

                queueDespawnerDeletion(i, j, p1, p2);
                ResolveSphereCapsuleCollision(mSphere, mCapA);

                if (!p1.isStatic) {
                    p1.velocity = mCapA.velocity;
                    p1.angularVelocity = mCapA.angularVelocity;
                    t1.position = (mCapA.capsule.Position() + mCapA.capsule.m_p2) * 0.5f;
                    t1.UpdateMatrix();
                }
                if (!p2.isStatic) {
                    p2.velocity = mSphere.velocity;
                    p2.angularVelocity = mSphere.angularVelocity;
                    t2.position = mSphere.sphere.Position();
                    t2.UpdateMatrix();
                }
                }

        }
    }

    // activeSpheres is now passed from Update()
    auto clothArray = registry.GetComponentArray<ClothComponent>();

    const size_t clothCount = clothArray->GetSize();
    for (size_t cIdx = 0; cIdx < clothCount; ++cIdx) {
        Entity c = clothArray->GetEntityAtIndex(cIdx);
        if (c == MAX_ENTITIES) continue;
        
        auto& cloth = clothArray->GetData(c);
        if (!cloth.collisionsEnabled || !cloth.dynamicGeometry || !cloth.dynamicGeometry->HasIndices()) continue;

        const auto& indices = cloth.dynamicGeometry->GetIndices();
        
        // Cache the set of particles to avoid re-construction in the inner loop
        // Note: For extreme optimization, we could store this set in the ClothComponent
        std::unordered_set<Entity> clothParticleSet(cloth.particles.begin(), cloth.particles.end());

        // Optimization: Calculate Cloth AABB once per sub-step to prune sphere checks
        glm::vec3 clothMin(std::numeric_limits<float>::max());
        glm::vec3 clothMax(std::numeric_limits<float>::lowest());
        bool hasValidParticles = false;
        for (Entity p : cloth.particles) {
            if (transformArray->HasData(p)) {
                const glm::vec3& pos = transformArray->GetData(p).position;
                clothMin = glm::min(clothMin, pos);
                clothMax = glm::max(clothMax, pos);
                hasValidParticles = true;
            }
        }

        if (!hasValidParticles) continue;

        for (Entity s : activeSpheres) {
            if (!registry.IsAlive(s)) continue;
            if (clothParticleSet.find(s) != clothParticleSet.end()) continue;

            auto& sphereTrans = transformArray->GetData(s);
            auto& sphereCol = colliderArray->GetData(s);
            auto& spherePhys = physicsArray->GetData(s);

            // Pruning: Skip this cloth entirely if the sphere is not near the cloth's AABB
            float r = sphereCol.radius;
            glm::vec3 sPos = sphereTrans.position;
            if (sPos.x + r < clothMin.x || sPos.x - r > clothMax.x ||
                sPos.y + r < clothMin.y || sPos.y - r > clothMax.y ||
                sPos.z + r < clothMin.z || sPos.z - r > clothMax.z) {
                continue;
            }

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
