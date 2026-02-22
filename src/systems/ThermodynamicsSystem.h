#pragma once
#include "ISystem.h"
#include <random>

class ThermodynamicsSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;

private:
    std::mt19937 m_Gen{ std::random_device{}() };
    float m_PrintTimer = 0.0f;
};