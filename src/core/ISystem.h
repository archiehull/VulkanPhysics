#pragma once

#include "ECS.h"

// Base interface for all ECS Systems
class ISystem {
public:
    virtual ~ISystem() = default;

    // Every system takes the registry and the delta time
    virtual void Update(Registry& registry, float deltaTime) = 0;
};