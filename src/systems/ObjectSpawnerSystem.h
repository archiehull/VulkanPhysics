#pragma once

#include "ISystem.h"

class ObjectSpawnerSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
    static void FireOnce(Scene& scene, Entity spawnerEntity);

private:
    static void SpawnObjectFromSpawner(Scene& scene, Entity spawnerEntity);
};
