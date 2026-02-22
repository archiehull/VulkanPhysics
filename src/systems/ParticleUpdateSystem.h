#pragma once
#include "ISystem.h"

class ParticleUpdateSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
};