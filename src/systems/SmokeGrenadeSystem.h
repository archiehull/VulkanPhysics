#pragma once
#include "ISystem.h"

class SmokeGrenadeSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
};
