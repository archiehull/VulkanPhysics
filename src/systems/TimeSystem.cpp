#include "TimeSystem.h"
#include "../rendering/Scene.h"
#include <iostream>

void TimeSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    // Iterate through all entities (there should only be one) with an EnvironmentComponent
    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<EnvironmentComponent>(e)) continue;

        auto& env = registry.GetComponent<EnvironmentComponent>(e);

        env.seasonTimer += deltaTime;
        const float fullSeasonDuration = env.timeConfig.dayLengthSeconds * static_cast<float>(env.timeConfig.daysPerSeason);

        // Check if it's time to transition to the next season
        if (env.seasonTimer >= fullSeasonDuration) {
            env.seasonTimer = 0.0f;

            // Advance the season
            env.currentSeason = static_cast<Season>((static_cast<int>(env.currentSeason) + 1) % 4);

            // Trigger Scene-level side effects (like switching precipitation particles)
            // Note: Once we extract the WeatherSystem, we won't need to call back into Scene for this!
            scene.NextSeason();
        }
    }
}