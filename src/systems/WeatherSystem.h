#pragma once
#include "ISystem.h"
#include "../core/Components.h"

class WeatherSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
    void PickNextWeatherDuration(EnvironmentComponent& env);


private:
};