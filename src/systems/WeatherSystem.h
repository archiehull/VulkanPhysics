#pragma once
#include "ISystem.h"
#include "../core/Components.h"

class WeatherSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;

private:
    void PickNextWeatherDuration(EnvironmentComponent& env);
};