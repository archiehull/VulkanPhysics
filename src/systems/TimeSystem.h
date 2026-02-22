#pragma once
#include "ISystem.h"

class TimeSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
};