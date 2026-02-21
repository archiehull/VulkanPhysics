#pragma once

#include "../core/ISystem.h"
#include "../core/Components.h"

class OrbitSystem : public ISystem {
public:
    void Update(Registry& registry, float deltaTime) override;
};