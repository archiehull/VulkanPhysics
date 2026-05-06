#define GLM_ENABLE_EXPERIMENTAL
#include "FlockSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include "SpatialGrid.h"
#include "SpatialOctree.h"
#include <glm/gtx/norm.hpp>
#include <vector>
#include <numeric>
#include <execution> // For std::execution::par (multithreading)
#include <chrono>    // For performance benchmarking

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
    std::vector<glm::vec3> boidPositions;
    const size_t boidCount = boidArray->GetSize();

    boids.reserve(boidCount);
    boidPositions.reserve(boidCount);

    for (size_t i = 0; i < boidCount; ++i) {
        Entity e = boidArray->GetEntityAtIndex(i);
        if (e != MAX_ENTITIES && transformArray->HasData(e) && physicsArray->HasData(e)) {
            boids.push_back(e);
            boidPositions.push_back(transformArray->GetData(e).position);
        }
    }

    if (boids.empty()) return;

    // ==========================================
    // BENCHMARK EXECUTION (Dry Run)
    // ==========================================
    if (manager->runBenchmarkRequested) {
        const float rSq = manager->perceptionRadius * manager->perceptionRadius;
        std::vector<size_t> idxs(boids.size());
        std::iota(idxs.begin(), idxs.end(), 0);

        // --- 1. NAIVE ---
        auto t0 = std::chrono::high_resolution_clock::now();
        // Naive build time is 0
        auto t1 = std::chrono::high_resolution_clock::now();
        std::for_each(std::execution::par, idxs.begin(), idxs.end(), [&](size_t i) {
            Entity myEnt = boids[i];
            const auto& myPos = transformArray->GetData(myEnt).position;
            int neighbors = 0;
            for (Entity otherEnt : boids) {
                if (myEnt == otherEnt) continue;
                float dSq = glm::length2(myPos - transformArray->GetData(otherEnt).position);
                if (dSq > 0.0f && dSq < rSq) neighbors++;
            }
            });
        auto t2 = std::chrono::high_resolution_clock::now();
        manager->benchmarkResults.naiveBuildMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
        manager->benchmarkResults.naiveQueryMs = std::chrono::duration<float, std::milli>(t2 - t1).count();

        // --- 2. UNIFORM GRID ---
        auto t3 = std::chrono::high_resolution_clock::now();
        static UniformGrid3D benchGrid;
        benchGrid.Build(manager->perceptionRadius, manager->boundsMin, manager->boundsMax, boids, boidPositions);
        auto t4 = std::chrono::high_resolution_clock::now();
        std::for_each(std::execution::par, idxs.begin(), idxs.end(), [&](size_t i) {
            Entity myEnt = boids[i];
            const auto& myPos = transformArray->GetData(myEnt).position;
            thread_local std::vector<Entity> localNeighbors;
            benchGrid.Query(myPos, manager->perceptionRadius, localNeighbors);
            int neighbors = 0;
            for (Entity otherEnt : localNeighbors) {
                if (myEnt == otherEnt) continue;
                float dSq = glm::length2(myPos - transformArray->GetData(otherEnt).position);
                if (dSq > 0.0f && dSq < rSq) neighbors++;
            }
            });
        auto t5 = std::chrono::high_resolution_clock::now();
        manager->benchmarkResults.gridBuildMs = std::chrono::duration<float, std::milli>(t4 - t3).count();
        manager->benchmarkResults.gridQueryMs = std::chrono::duration<float, std::milli>(t5 - t4).count();

        // --- 3. OCTREE (Stubbed for now) ---
        auto t6 = std::chrono::high_resolution_clock::now();
        static SpatialOctree benchOctree(16);
        benchOctree.Build(manager->boundsMin, manager->boundsMax, boids, boidPositions);

        auto t7 = std::chrono::high_resolution_clock::now();
        std::for_each(std::execution::par, idxs.begin(), idxs.end(), [&](size_t i) {
            Entity myEnt = boids[i];
            const auto& myPos = transformArray->GetData(myEnt).position;

            thread_local std::vector<Entity> localNeighbors;
            benchOctree.Query(myPos, manager->perceptionRadius, localNeighbors);

            int neighbors = 0;
            for (Entity otherEnt : localNeighbors) {
                if (myEnt == otherEnt) continue;
                // Distance check already partially done in Octree Query, 
                // but we do it here again to match the other benchmarks exactly
                float dSq = glm::length2(myPos - transformArray->GetData(otherEnt).position);
                if (dSq > 0.0f && dSq < rSq) neighbors++;
            }
            });
        auto t8 = std::chrono::high_resolution_clock::now();

        manager->benchmarkResults.octreeBuildMs = std::chrono::duration<float, std::milli>(t7 - t6).count();
        manager->benchmarkResults.octreeQueryMs = std::chrono::duration<float, std::milli>(t8 - t7).count();

        // Finish benchmark
        manager->benchmarkResults.hasData = true;
        manager->benchmarkResults.isRunning = false;
        manager->runBenchmarkRequested = false;
    }

    // ==========================================
    // PHASE 1: BUILD SPATIAL STRUCTURE
    // ==========================================
    auto buildStart = std::chrono::high_resolution_clock::now();

    // Static so we don't reallocate the underlying vectors every single frame
    static UniformGrid3D grid;
    static SpatialOctree octree(16); // Capacity of 16 boids per node

    if (manager->partitionType == SpatialPartitionType::UniformGrid) {
        grid.Build(manager->perceptionRadius, manager->boundsMin, manager->boundsMax, boids, boidPositions);
        manager->memoryUsedBytes = grid.GetMemoryUsage();
    }
    else if (manager->partitionType == SpatialPartitionType::Octree) {
        octree.Build(manager->boundsMin, manager->boundsMax, boids, boidPositions);
        manager->memoryUsedBytes = octree.GetMemoryUsage();
    }
    else {
        manager->memoryUsedBytes = 0; // Naive uses no extra memory
    }

    auto buildEnd = std::chrono::high_resolution_clock::now();
    manager->lastBuildTimeMs = std::chrono::duration<float, std::milli>(buildEnd - buildStart).count();

    // ==========================================
    // PHASE 2: QUERY & APPLY FORCES (Parallel)
    // ==========================================
    struct BoidUpdate {
        glm::vec3 newPosition;
        glm::vec3 newVelocity;
    };

    std::vector<BoidUpdate> updates(boids.size());
    std::vector<size_t> indices(boids.size());
    std::iota(indices.begin(), indices.end(), 0);

    const float perceptionRadiusSq = manager->perceptionRadius * manager->perceptionRadius;

    auto queryStart = std::chrono::high_resolution_clock::now();

    // Run parallel calculations
    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t i) {
        Entity myEntity = boids[i];
        const auto& myTransform = transformArray->GetData(myEntity);
        const auto& myPhysics = physicsArray->GetData(myEntity);

        glm::vec3 separation(0.0f);
        glm::vec3 alignment(0.0f);
        glm::vec3 cohesion(0.0f);
        int neighbors = 0;

        // --- SPATIAL QUERY ---
        // Thread-local vector to avoid allocation overhead during query
        thread_local std::vector<Entity> potentialNeighbors;

        if (manager->partitionType == SpatialPartitionType::UniformGrid) {
            grid.Query(myTransform.position, manager->perceptionRadius, potentialNeighbors);
        }
        else if (manager->partitionType == SpatialPartitionType::Octree) {
            octree.Query(myTransform.position, manager->perceptionRadius, potentialNeighbors);
        }
        else {
            potentialNeighbors = boids; // Naive: everyone is a potential neighbor
        }

        // --- DISTANCE CHECK ---
        for (Entity otherEntity : potentialNeighbors) {
            if (myEntity == otherEntity) continue;

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

    auto queryEnd = std::chrono::high_resolution_clock::now();
    manager->lastQueryTimeMs = std::chrono::duration<float, std::milli>(queryEnd - queryStart).count();

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