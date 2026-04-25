#pragma once

#include "../rendering/Scene.h"
#include "ISystem.h"

class ClothSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
};
