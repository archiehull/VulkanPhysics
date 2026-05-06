#define GLM_ENABLE_EXPERIMENTAL

#include "FlockSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include <glm/gtx/norm.hpp>
#include <vector>
#include <numeric>
#include <execution> // For std::execution::par (multithreading)

glm::vec3 FlockSystem::Limit(const glm::vec3& v, float max) const {
    float lengthSq = glm::length2(v);
    if (lengthSq > max * max && lengthSq > 0.0f) {
        return glm::normalize(v) * max;
    }
    return v;
}

glm::vec3 FlockSystem::Wraparound(glm::vec3 pos, const glm::vec3& minBounds, const glm::vec3& maxBounds) const {
    for (int i = 0; i < 3; ++i) {
        float width = maxBounds[i] - minBounds[i];
        if (pos[i] < minBounds[i]) {
            pos[i] = maxBounds[i] - std::fmod(minBounds[i] - pos[i], width);
        }
        else if (pos[i] > maxBounds[i]) {
            pos[i] = minBounds[i] + std::fmod(pos[i] - maxBounds[i], width);
        }
    }
    return pos;
}

void FlockSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    // 1. Locate the FlockManager (Global config)
    FlockManagerComponent* manager = nullptr;
    auto managerArray = registry.GetComponentArray<FlockManagerComponent>();
    if (managerArray->GetSize() > 0) {
        manager = &managerArray->GetData(managerArray->GetEntityAtIndex(0));
    }

    if (!manager) return; // No flock to simulate

    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();
    auto boidArray = registry.GetComponentArray<BoidComponent>();

    // 2. Gather all participating boids
    std::vector<Entity> boids;
    const size_t boidCount = boidArray->GetSize();
    boids.reserve(boidCount);
    for (size_t i = 0; i < boidCount; ++i) {
        Entity e = boidArray->GetEntityAtIndex(i);
        if (e != MAX_ENTITIES && transformArray->HasData(e) && physicsArray->HasData(e)) {
            boids.push_back(e);
        }
    }

    if (boids.empty()) return;

    // 3. Multithreaded Snapshot Calculations (Matches Rust's ThreadPool clone behavior)
    struct BoidUpdate {
        glm::vec3 newPosition;
        glm::vec3 newVelocity;
    };
    std::vector<BoidUpdate> updates(boids.size());

    std::vector<size_t> indices(boids.size());
    std::iota(indices.begin(), indices.end(), 0);

    const float perceptionRadiusSq = manager->perceptionRadius * manager->perceptionRadius;

    // Run parallel calculations
    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
        Entity myEntity = boids[i];
        const auto& myTransform = transformArray->GetData(myEntity);
        const auto& myPhysics = physicsArray->GetData(myEntity);

        glm::vec3 separation(0.0f);
        glm::vec3 alignment(0.0f);
        glm::vec3 cohesion(0.0f);
        int neighbors = 0;

        for (size_t j = 0; j < boids.size(); ++j) {
            if (i == j) continue;

            Entity otherEntity = boids[j];
            const auto& otherTransform = transformArray->GetData(otherEntity);
            const auto& otherPhysics = physicsArray->GetData(otherEntity);

            glm::vec3 diff = myTransform.position - otherTransform.position;
            float distSq = glm::length2(diff);

            if (distSq > 0.0f && distSq < perceptionRadiusSq) {
                float dist = std::sqrt(distSq);
                separation += diff / dist;
                alignment += otherPhysics.velocity;
                cohesion += otherTransform.position;
                neighbors++;
            }
        }

        glm::vec3 acceleration(0.0f);

        if (neighbors > 0) {
            float fNeighbors = static_cast<float>(neighbors);

            // Separation
            separation /= fNeighbors;
            glm::vec3 sepForce(0.0f);
            if (glm::length2(separation) > 0.0f) {
                sepForce = Limit(glm::normalize(separation) * manager->maxSpeed - myPhysics.velocity, manager->maxForce);
            }

            // Alignment
            alignment /= fNeighbors;
            glm::vec3 alignForce(0.0f);
            if (glm::length2(alignment) > 0.0f) {
                alignForce = Limit(glm::normalize(alignment) * manager->maxSpeed - myPhysics.velocity, manager->maxForce);
            }

            // Cohesion
            cohesion /= fNeighbors;
            cohesion -= myTransform.position;
            glm::vec3 cohForce(0.0f);
            if (glm::length2(cohesion) > 0.0f) {
                cohForce = Limit(glm::normalize(cohesion) * manager->maxSpeed - myPhysics.velocity, manager->maxForce);
            }

            acceleration = (sepForce * manager->separationWeight) +
                (alignForce * manager->alignmentWeight) +
                (cohForce * manager->cohesionWeight);
        }

        // Apply forces with deltaTime scaling (normalized to 60fps for parameter consistency)
        float dtScale = deltaTime * 60.0f;
        glm::vec3 velocity = myPhysics.velocity + (acceleration * dtScale);
        velocity = Limit(velocity, manager->maxSpeed);
        glm::vec3 position = Wraparound(myTransform.position + (velocity * dtScale), manager->boundsMin, manager->boundsMax);

        updates[i] = { position, velocity };
        });

    // 4. Apply synchronized updates sequentially
    for (size_t i = 0; i < boids.size(); ++i) {
        Entity boid = boids[i];
        auto& transform = transformArray->GetData(boid);
        auto& physics = physicsArray->GetData(boid);

        transform.position = updates[i].newPosition;
        physics.velocity = updates[i].newVelocity;

        // Auto-orient the bird geometry to face the direction of flight
        if (glm::length2(physics.velocity) > 0.0001f) {
            glm::vec3 dir = glm::normalize(physics.velocity);
            float yaw = std::atan2(dir.x, dir.z);
            float pitch = -std::asin(glm::clamp(dir.y, -1.0f, 1.0f));
            transform.rotation = glm::degrees(glm::vec3(pitch, yaw + glm::pi<float>(), 0.0f));
        }

        transform.UpdateMatrix();
    }
}