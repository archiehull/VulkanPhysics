#pragma once
#include "ISystem.h"

class CameraSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
};