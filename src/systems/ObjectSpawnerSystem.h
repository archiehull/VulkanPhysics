#pragma once

#include "ISystem.h"

class ObjectSpawnerSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;

private:
    void SpawnObjectFromSpawner(Scene& scene, Entity spawnerEntity);
};
