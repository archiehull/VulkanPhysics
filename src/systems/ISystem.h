#pragma once

#include "../core/ECS.h"

class Scene;

// Base interface for all ECS Systems
class ISystem {
public:
    virtual ~ISystem() = default;

    // Every system takes the registry and the delta time
    virtual void Update(Scene& scene, float deltaTime) = 0;
};