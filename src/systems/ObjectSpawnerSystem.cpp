#include "ObjectSpawnerSystem.h"
#include "../rendering/Scene.h"
#include "../core/Components.h"
#include <algorithm>
#include <random>

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
        if (!spawner.enabled) continue;

        const float interval = std::max(0.01f, spawner.spawnInterval);
        spawner.spawnTimer += deltaTime;

        while (spawner.spawnTimer >= interval) {
            spawner.spawnTimer -= interval;
            SpawnObjectFromSpawner(scene, e);
        }
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
        spawnerName = registry.GetComponent<NameComponent>(spawnerEntity).name;
    }

    const std::string spawnedName = spawnerName + "_Spawned_" + std::to_string(spawner.spawnedCount++);
    const glm::vec3 spawnPos = transform.position;

    const std::string& geometryType = spawner.spawnGeometryType;
    if (geometryType == "Cube") {
        scene.AddCube(spawnedName, spawnPos, spawner.spawnScale, spawner.spawnTexturePath);
    }
    else if (geometryType == "Model" && !spawner.spawnModelPath.empty()) {
        scene.AddModel(spawnedName, spawnPos, glm::vec3(0.0f), spawner.spawnScale, spawner.spawnModelPath, spawner.spawnTexturePath, false);
    }
    else {
        const float radius = std::max(0.1f, std::max({ spawner.spawnScale.x, spawner.spawnScale.y, spawner.spawnScale.z }) * 0.5f);
        scene.AddSphere(spawnedName, 16, 32, radius, spawnPos, spawner.spawnTexturePath);
    }

    Entity spawnedEntity = scene.GetEntityByName(spawnedName);
    if (spawnedEntity == MAX_ENTITIES) return;

    registry.AddComponent<SpawnedFromSpawnerComponent>(spawnedEntity, { spawnerEntity });

    scene.SetObjectCollision(spawnedName, true);
    scene.SetObjectPhysics(spawnedName, false, std::max(0.01f, spawner.spawnMass));
    scene.SetObjectCollider(spawnedName, 0, std::max(0.1f, std::max({ spawner.spawnScale.x, spawner.spawnScale.y, spawner.spawnScale.z }) * 0.5f), glm::vec3(0.0f, 1.0f, 0.0f));

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

    if (registry.HasComponent<PhysicsComponent>(spawnedEntity)) {
        auto& phys = registry.GetComponent<PhysicsComponent>(spawnedEntity);
        phys.isStatic = false;
        phys.velocity = velocity;
    }
}
