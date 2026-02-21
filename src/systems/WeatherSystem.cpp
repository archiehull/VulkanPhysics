#include "WeatherSystem.h"
#include "../rendering/Scene.h"
#include <random>
#include <algorithm>
#include <iostream>

void WeatherSystem::PickNextWeatherDuration(EnvironmentComponent& env) {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    if (env.isPrecipitating) {
        std::uniform_real_distribution<float> dist(env.weatherConfig.minPrecipitationDuration, env.weatherConfig.maxPrecipitationDuration);
        env.currentWeatherDurationTarget = dist(gen);
    }
    else {
        std::uniform_real_distribution<float> dist(env.weatherConfig.minClearInterval, env.weatherConfig.maxClearInterval);
        env.currentWeatherDurationTarget = dist(gen);
    }
}

void WeatherSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    // Find the environment singleton
    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<EnvironmentComponent>(e)) continue;

        auto& env = registry.GetComponent<EnvironmentComponent>(e);

        // --- 1. Rain & Dust Timers ---
        if (env.isPrecipitating) {
            env.timeSinceLastRain = 0.0f;
            // The Scene still holds the raw ParticleSystem for dust, so we call it here
            scene.StopDust();
            env.postRainFireSuppressionTimer = env.weatherConfig.fireSuppressionDuration;
        }
        else {
            env.timeSinceLastRain += deltaTime;
            if (env.postRainFireSuppressionTimer > 0.0f) {
                env.postRainFireSuppressionTimer -= deltaTime;
            }
            // If it hasn't rained in 60 seconds, trigger dust cloud
            if (!scene.IsDustActive() && env.timeSinceLastRain >= 60.0f) {
                scene.SpawnDustCloud();
            }
        }

        // --- 2. Precipitation State Machine ---
        env.weatherTimer += deltaTime;
        if (env.weatherTimer >= env.currentWeatherDurationTarget) {
            env.weatherTimer = 0.0f;
            env.isPrecipitating = !env.isPrecipitating;
            PickNextWeatherDuration(env);

            if (env.isPrecipitating) {
                if (env.currentSeason == Season::WINTER) scene.AddSnow();
                else scene.AddRain();
            }
            else {
                scene.StopPrecipitation();
            }
        }

        // --- 3. Sun Height & Temperature Calculations ---
        float sunHeight = 0.0f;
        Entity sunEntity = scene.GetEntityByName("Sun");

        if (sunEntity != MAX_ENTITIES && registry.HasComponent<TransformComponent>(sunEntity)) {
            const auto& sunTransform = registry.GetComponent<TransformComponent>(sunEntity);
            sunHeight = std::clamp(sunTransform.matrix[3][1] / 275.0f, -1.0f, 1.0f);
        }

        // Store it in the component for the Thermodynamics System to read!
        env.currentSunHeight = sunHeight;

        float seasonBaseTemp = 0.0f;
        glm::vec3 targetSunColor = glm::vec3(1.0f);

        switch (env.currentSeason) {
        case Season::SUMMER:
            seasonBaseTemp = env.seasonConfig.summerBaseTemp;
            targetSunColor = glm::vec3(1.0f, 0.95f, 0.8f);
            break;
        case Season::AUTUMN:
            seasonBaseTemp = (env.seasonConfig.summerBaseTemp + env.seasonConfig.winterBaseTemp) * 0.5f;
            targetSunColor = glm::vec3(1.0f, 0.85f, 0.7f);
            break;
        case Season::WINTER:
            seasonBaseTemp = env.seasonConfig.winterBaseTemp;
            targetSunColor = glm::vec3(0.75f, 0.85f, 1.0f);
            break;
        case Season::SPRING:
            seasonBaseTemp = (env.seasonConfig.summerBaseTemp + env.seasonConfig.winterBaseTemp) * 0.5f;
            targetSunColor = glm::vec3(1.0f, 0.98f, 0.9f);
            break;
        }

        // Base temperature for the scene
        env.weatherIntensity = seasonBaseTemp + (sunHeight * env.seasonConfig.dayNightTempDiff);

        // Apply weather penalties
        if (env.isPrecipitating) {
            targetSunColor = glm::vec3(0.4f, 0.45f, 0.55f);
            env.weatherIntensity -= 10.0f;
        }

        // Update Sun Light Component
        if (sunEntity != MAX_ENTITIES && registry.HasComponent<LightComponent>(sunEntity)) {
            auto& sunLight = registry.GetComponent<LightComponent>(sunEntity);
            sunLight.color = glm::mix(sunLight.color, targetSunColor, deltaTime * 0.8f);
        }
    }
}