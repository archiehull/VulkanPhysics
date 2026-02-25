#pragma once

#include "ParticleSystem.h"
#include <string>
#include <vector>
#include <utility>

namespace ParticleLibrary {
    const ParticleProps& GetFireProps();
    const ParticleProps& GetSmokeProps();
    const ParticleProps& GetRainProps();
    const ParticleProps& GetSnowProps();
    const ParticleProps& GetDustProps();
    const ParticleProps& GetDustStormProps();

    // New function to return all presets dynamically
    std::vector<std::pair<std::string, ParticleProps>> GetAllPresets();
}