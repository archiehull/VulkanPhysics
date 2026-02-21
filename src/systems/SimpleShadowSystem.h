#pragma once
#include "ISystem.h"
#include "../rendering/Scene.h"

class SimpleShadowSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
};