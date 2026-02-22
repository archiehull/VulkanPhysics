#pragma once
#include "ISystem.h"

class OrbitSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
};