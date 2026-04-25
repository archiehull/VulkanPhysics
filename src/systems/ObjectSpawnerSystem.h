#pragma once

#include "ISystem.h"
#include <string>

struct ObjectSpawnerComponent;

class ObjectSpawnerSystem : public ISystem {
public:
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
