#pragma once

#include "ISystem.h"
#include <string>
#include <functional>
#include <glm/glm.hpp>

struct ObjectSpawnerComponent;

class ObjectSpawnerSystem : public ISystem {
public:
    struct SpawnEvent {
        Entity entityId;
        std::string geometryType;
        glm::vec3 position;
        glm::vec3 scale;
        std::string texturePath;
        std::string modelPath;
        float mass;
        glm::vec3 velocity;
        glm::vec3 angularVelocity;
        uint8_t ownerId;
    };

    using SpawnCallback = std::function<void(const SpawnEvent&)>;
    static SpawnCallback onObjectSpawned;

    void Update(Scene& scene, float deltaTime) override;
    static void FireOnce(Scene& scene, Entity spawnerEntity);
    static void FireGroup(Scene& scene, const std::string& group);
    static void StartSpawner(Scene& scene, Entity spawnerEntity);
    static void StartGroup(Scene& scene, const std::string& group);
    static void TriggerStartupSpawners(Scene& scene);

private:
    static void SpawnObjectFromSpawner(Scene& scene, Entity spawnerEntity);
    static void ResetSpawnerRun(ObjectSpawnerComponent& spawner);
};
