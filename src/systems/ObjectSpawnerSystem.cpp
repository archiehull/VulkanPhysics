#include "ObjectSpawnerSystem.h"
#include "../rendering/Scene.h"
#include "../core/Components.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>

namespace {
    std::string NormalizeSpawnerGroup(const std::string& group) {
        if (group.empty()) return "A";
        char g = static_cast<char>(std::toupper(static_cast<unsigned char>(group[0])));
        if (g < 'A' || g > 'D') {
            g = 'A';
        }
        return std::string(1, g);
    }

    bool IsValidSpawner(Scene& scene, Entity entity) {
        auto& registry = scene.GetRegistry();
        return registry.HasComponent<ObjectSpawnerComponent>(entity) &&
            registry.HasComponent<TransformComponent>(entity);
    }
}

void ObjectSpawnerSystem::ResetSpawnerRun(ObjectSpawnerComponent& spawner) {
    spawner.spawnTimer = 0.0f;
    spawner.runElapsedSeconds = 0.0f;
    spawner.spawnedThisRun = 0;
}

void ObjectSpawnerSystem::Update(Scene& scene, float deltaTime) {
    if (deltaTime <= 0.0f) return;

    auto& registry = scene.GetRegistry();
    const Entity count = registry.GetEntityCount();

    for (Entity e = 0; e < count; ++e) {
        if (!registry.HasComponent<ObjectSpawnerComponent>(e) ||
            !registry.HasComponent<TransformComponent>(e)) {
            continue; 
        }

        auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
        if (spawner.alwaysOn) {
            spawner.isRunning = true;
            spawner.runDurationSeconds = -1.0f;
            spawner.maxSpawnsPerRun = -1;
        }
        if (!spawner.isRunning) continue;

        if (spawner.maxSpawnsPerRun >= 0 && spawner.spawnedThisRun >= spawner.maxSpawnsPerRun) {
            if (!spawner.alwaysOn) spawner.isRunning = false;
            continue;
        }

        spawner.runElapsedSeconds += deltaTime;
        if (!spawner.alwaysOn && spawner.runDurationSeconds > 0.0f && spawner.runElapsedSeconds >= spawner.runDurationSeconds) {
            spawner.isRunning = false;
            continue;
        }

        const float interval = std::max(0.01f, spawner.spawnInterval);
        spawner.spawnTimer += deltaTime;

        constexpr int kMaxCatchUpSpawnsPerFrame = 4;
        int spawnsThisFrame = 0;

        while (spawner.spawnTimer >= interval && spawnsThisFrame < kMaxCatchUpSpawnsPerFrame) {
            if (spawner.maxSpawnsPerRun >= 0 && spawner.spawnedThisRun >= spawner.maxSpawnsPerRun) {
                if (!spawner.alwaysOn) spawner.isRunning = false;
                break;
            }

            if (!spawner.alwaysOn && spawner.runDurationSeconds > 0.0f && spawner.runElapsedSeconds >= spawner.runDurationSeconds) {
                spawner.isRunning = false;
                break;
            }

            spawner.spawnTimer -= interval;
            SpawnObjectFromSpawner(scene, e);
            ++spawnsThisFrame;
        }

        if (spawner.spawnTimer > interval) {
            // Prevent unbounded backlog: keep at most one interval of debt to smooth spawns over frames.
            spawner.spawnTimer = std::fmod(spawner.spawnTimer, interval);
        }
    }
}

void ObjectSpawnerSystem::FireOnce(Scene& scene, Entity spawnerEntity) {
    if (!IsValidSpawner(scene, spawnerEntity)) {
        return;
    }

    SpawnObjectFromSpawner(scene, spawnerEntity);
}

void ObjectSpawnerSystem::FireGroup(Scene& scene, const std::string& group) {
    auto& registry = scene.GetRegistry();
    const Entity count = registry.GetEntityCount();
    const std::string normalizedGroup = NormalizeSpawnerGroup(group);

    for (Entity e = 0; e < count; ++e) {
        if (!registry.HasComponent<ObjectSpawnerComponent>(e) ||
            !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
        if (NormalizeSpawnerGroup(spawner.group) != normalizedGroup) {
            continue;
        }

        SpawnObjectFromSpawner(scene, e);
    }
}

void ObjectSpawnerSystem::StartSpawner(Scene& scene, Entity spawnerEntity) {
    if (!IsValidSpawner(scene, spawnerEntity)) {
        return;
    }

    auto& spawner = scene.GetRegistry().GetComponent<ObjectSpawnerComponent>(spawnerEntity);
    ResetSpawnerRun(spawner);
    spawner.isRunning = true;
}

void ObjectSpawnerSystem::StartGroup(Scene& scene, const std::string& group) {
    auto& registry = scene.GetRegistry();
    const Entity count = registry.GetEntityCount();
    const std::string normalizedGroup = NormalizeSpawnerGroup(group);

    for (Entity e = 0; e < count; ++e) {
        if (!IsValidSpawner(scene, e)) {
            continue;
        }

        auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
        if (NormalizeSpawnerGroup(spawner.group) != normalizedGroup) {
            continue;
        }

        StartSpawner(scene, e);
    }
}

void ObjectSpawnerSystem::TriggerStartupSpawners(Scene& scene) {
    auto& registry = scene.GetRegistry();
    const Entity count = registry.GetEntityCount();

    for (Entity e = 0; e < count; ++e) {
        if (!IsValidSpawner(scene, e)) {
            continue;
        }

        auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
        if (!spawner.triggerOnStartup) {
            continue;
        }

        StartSpawner(scene, e);
    }
}

void ObjectSpawnerSystem::SpawnObjectFromSpawner(Scene& scene, Entity spawnerEntity) {
    auto& registry = scene.GetRegistry();
    if (!registry.HasComponent<ObjectSpawnerComponent>(spawnerEntity) ||
        !registry.HasComponent<TransformComponent>(spawnerEntity)) {
        return;
    }

    auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(spawnerEntity);
    const auto& transform = registry.GetComponent<TransformComponent>(spawnerEntity);

    std::string spawnerName = "Spawner";
    if (registry.HasComponent<NameComponent>(spawnerEntity)) {
        std::string name = registry.GetComponent<NameComponent>(spawnerEntity).name;
        if (!name.empty()) {
            spawnerName = name;
        }
    }

    const std::string spawnedName = spawnerName + "_Spawned_" + std::to_string(spawner.spawnedCount++);
    spawner.spawnedThisRun++;
    const glm::vec3 spawnPos = transform.position;

    const std::string& geometryType = spawner.spawnGeometryType;
    glm::vec3 effectiveSpawnScale = spawner.spawnScale;
    float sphereBaseRadius = 0.5f;
    if (geometryType == "Sphere") {
        const float uniform = std::max(0.05f, spawner.spawnObjectScale);
        const glm::vec3 axisScale(
            std::max(0.05f, spawner.spawnScale.x),
            std::max(0.05f, spawner.spawnScale.y),
            std::max(0.05f, spawner.spawnScale.z));

        sphereBaseRadius = std::max(0.1f, 0.5f * uniform);
        // Final sphere world extents = base radius (uniform) * axis scale (XYZ)
        effectiveSpawnScale = axisScale * uniform;
    }

    if (geometryType == "Cube") {
        scene.AddCube(spawnedName, spawnPos, effectiveSpawnScale, spawner.spawnTexturePath);
    }
    else if (geometryType == "Model" && !spawner.spawnModelPath.empty()) {
        scene.AddModel(spawnedName, spawnPos, glm::vec3(0.0f), effectiveSpawnScale, spawner.spawnModelPath, spawner.spawnTexturePath, false);
    }
    else {
        scene.AddSphere(spawnedName, 16, 32, sphereBaseRadius, spawnPos, spawner.spawnTexturePath);
        // Apply XYZ scaling for spheres (ellipsoid support) while keeping Object Scale as uniform size.
        scene.SetObjectTransform(
            spawnedName,
            spawnPos,
            glm::vec3(0.0f),
            glm::vec3(
                std::max(0.05f, spawner.spawnScale.x),
                std::max(0.05f, spawner.spawnScale.y),
                std::max(0.05f, spawner.spawnScale.z)));
    }

    Entity spawnedEntity = scene.GetEntityByName(spawnedName);
    if (spawnedEntity == MAX_ENTITIES) return;

    registry.AddComponent<SpawnedFromSpawnerComponent>(spawnedEntity, { spawnerEntity });

    scene.SetObjectCollision(spawnedName, true);
    scene.SetObjectPhysics(spawnedName, false, std::max(0.01f, spawner.spawnMass));
    scene.SetObjectCollider(spawnedName, 0, std::max(0.1f, std::max({ effectiveSpawnScale.x, effectiveSpawnScale.y, effectiveSpawnScale.z }) * 0.5f), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::vec3 velocity = spawner.spawnVelocity;
    if (spawner.randomizeVelocity) {
        static std::mt19937 rng(std::random_device{}());

        std::uniform_real_distribution<float> distX(-spawner.randomVelocityRange.x, spawner.randomVelocityRange.x);
        std::uniform_real_distribution<float> distY(-spawner.randomVelocityRange.y, spawner.randomVelocityRange.y);
        std::uniform_real_distribution<float> distZ(-spawner.randomVelocityRange.z, spawner.randomVelocityRange.z);

        velocity.x += distX(rng);
        velocity.y += distY(rng);
        velocity.z += distZ(rng);
    }

    // Prepare angular velocity
    glm::vec3 angVel = spawner.spawnAngularVelocity;
    if (spawner.randomizeAngularVelocity) {
        static std::mt19937 rng_ang(std::random_device{}());
        std::uniform_real_distribution<float> aX(-spawner.randomAngularVelocityRange.x, spawner.randomAngularVelocityRange.x);
        std::uniform_real_distribution<float> aY(-spawner.randomAngularVelocityRange.y, spawner.randomAngularVelocityRange.y);
        std::uniform_real_distribution<float> aZ(-spawner.randomAngularVelocityRange.z, spawner.randomAngularVelocityRange.z);
        angVel.x += aX(rng_ang);
        angVel.y += aY(rng_ang);
        angVel.z += aZ(rng_ang);
    }

    if (registry.HasComponent<PhysicsComponent>(spawnedEntity)) {
        auto& phys = registry.GetComponent<PhysicsComponent>(spawnedEntity);
        phys.isStatic = false;
        phys.velocity = velocity;
        phys.angularVelocity = angVel; // assign initial spin
    }
}
