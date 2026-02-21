#include "ThermodynamicsSystem.h"
#include "../rendering/Scene.h"
#include "../rendering/ParticleLibrary.h"
#include <random>
#include <glm/gtc/matrix_transform.hpp>

void ThermodynamicsSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();
    Entity envEntity = scene.GetEnvironmentEntity();
    if (envEntity == MAX_ENTITIES) return;

    auto& env = registry.GetComponent<EnvironmentComponent>(envEntity);

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<ThermoComponent>(e) ||
            !registry.HasComponent<TransformComponent>(e) ||
            !registry.HasComponent<RenderComponent>(e)) {
            continue;
        }

        auto& thermo = registry.GetComponent<ThermoComponent>(e);
        if (!thermo.isFlammable) continue;

        auto& transform = registry.GetComponent<TransformComponent>(e);
        auto& render = registry.GetComponent<RenderComponent>(e);

        const glm::vec3 basePos = glm::vec3(transform.matrix[3]);

        switch (thermo.state) {
        case ObjectState::NORMAL:
        case ObjectState::HEATING: {
            const float responseSpeed = thermo.thermalResponse;
            const float effectiveIgnitionThreshold = 100.0f;
            float targetTemp = env.weatherIntensity;

            // Use the Sun Height from the Environment Component!
            if (env.currentSunHeight > 0.1f) {
                targetTemp += env.sunHeatBonus * env.currentSunHeight;
            }

            if (env.isPrecipitating) {
                targetTemp -= 40.0f;
            }

            const float changeRate = responseSpeed * deltaTime;
            const float lerpFactor = glm::clamp(changeRate, 0.0f, 1.0f);
            thermo.currentTemp = glm::mix(thermo.currentTemp, targetTemp, lerpFactor);

            if (thermo.currentTemp > 45.0f) {
                thermo.state = ObjectState::HEATING;
            }
            else {
                thermo.state = ObjectState::NORMAL;
            }

            if (!env.isPrecipitating && env.postRainFireSuppressionTimer <= 0.0f && thermo.currentTemp >= effectiveIgnitionThreshold) {
                const float excessHeat = thermo.currentTemp - effectiveIgnitionThreshold;
                const float ignitionChancePerSecond = 0.05f + (excessHeat * 0.005f);

                if (chance(gen) < (ignitionChancePerSecond * deltaTime)) {
                    thermo.state = ObjectState::BURNING;
                    thermo.burnTimer = 0.0f;
                    thermo.fireEmitterId = scene.AddFire(basePos, 0.1f);
                    thermo.smokeEmitterId = scene.AddSmoke(basePos, 0.1f);
                }
            }
            break;
        }

        case ObjectState::BURNING: {
            if (env.isPrecipitating) {
                scene.StopObjectFire(e);
                thermo.state = ObjectState::NORMAL;
                thermo.currentTemp = env.weatherIntensity;
                thermo.burnTimer = 0.0f;
                break;
            }

            thermo.currentTemp += thermo.selfHeatingRate * deltaTime;
            thermo.burnTimer += deltaTime;

            const float growth = glm::clamp(thermo.burnTimer / (thermo.maxBurnDuration * 0.6f), 0.0f, 1.0f);
            thermo.burnFactor = glm::clamp(thermo.burnTimer / thermo.maxBurnDuration, 0.0f, 1.0f);

            const float maxFireHeight = 3.0f;
            const float currentFireHeight = 0.2f + (maxFireHeight - 0.2f) * growth;

            if (thermo.fireEmitterId != -1) {
                ParticleProps fireProps = ParticleLibrary::GetFireProps();
                fireProps.position = basePos;
                fireProps.position.y += currentFireHeight * 0.5f;
                fireProps.positionVariation = glm::vec3(0.3f, currentFireHeight * 0.4f, 0.3f);

                const float particleScale = 1.0f + growth * 0.5f;
                fireProps.sizeBegin *= particleScale;
                fireProps.sizeEnd *= particleScale;

                const float rate = 50.0f + (300.0f * growth);
                scene.GetOrCreateSystem(fireProps)->UpdateEmitter(thermo.fireEmitterId, fireProps, rate);
            }

            if (thermo.smokeEmitterId != -1) {
                ParticleProps smokeProps = ParticleLibrary::GetSmokeProps();
                smokeProps.position = basePos;
                smokeProps.position.y += currentFireHeight;

                const float smokeScale = 1.0f + growth * 2.0f;
                smokeProps.sizeBegin *= smokeScale;
                smokeProps.sizeEnd *= smokeScale;
                smokeProps.lifeTime = 8.0f;
                smokeProps.velocity.y = 3.0f;

                const float rate = 20.0f + (80.0f * growth);
                scene.GetOrCreateSystem(smokeProps)->UpdateEmitter(thermo.smokeEmitterId, smokeProps, rate);
            }

            glm::vec3 lightPos = basePos;
            lightPos.y += currentFireHeight * 0.5f;

            if (thermo.fireLightEntity == -1 || thermo.fireLightEntity == MAX_ENTITIES) {
                std::string lightName = "FireLight_" + std::to_string(e);
                thermo.fireLightEntity = scene.AddLight(lightName, lightPos, glm::vec3(1.0f, 0.5f, 0.1f), 0.0f, 1);
            }

            if (thermo.fireLightEntity != MAX_ENTITIES && registry.HasComponent<LightComponent>(thermo.fireLightEntity)) {
                auto& fireLightTransform = registry.GetComponent<TransformComponent>(thermo.fireLightEntity);
                auto& fireLightComp = registry.GetComponent<LightComponent>(thermo.fireLightEntity);

                const float t = thermo.burnTimer;
                const float flicker = 1.0f + 0.3f * std::sin(t * 15.0f) + 0.15f * std::sin(t * 37.0f);
                const float targetIntensity = 50.05f * growth;

                fireLightTransform.matrix[3] = glm::vec4(lightPos, 1.0f);
                fireLightComp.intensity = targetIntensity * flicker;
            }

            if (thermo.burnTimer >= thermo.maxBurnDuration) {
                thermo.state = ObjectState::BURNT;

                if (thermo.fireEmitterId != -1) {
                    scene.GetOrCreateSystem(ParticleLibrary::GetFireProps())->StopEmitter(thermo.fireEmitterId);
                    thermo.fireEmitterId = -1;
                }

                if (thermo.fireLightEntity != MAX_ENTITIES && registry.HasComponent<LightComponent>(thermo.fireLightEntity)) {
                    registry.GetComponent<LightComponent>(thermo.fireLightEntity).intensity = 0.0f;
                }

                if (thermo.smokeEmitterId != -1) {
                    ParticleProps smolder = ParticleLibrary::GetSmokeProps();
                    smolder.position = basePos;
                    smolder.sizeBegin *= 0.1f;
                    smolder.sizeEnd *= 0.2f;
                    smolder.lifeTime = 1.5f;
                    smolder.velocity.y = 0.5f;
                    smolder.positionVariation = glm::vec3(0.1f);
                    scene.GetOrCreateSystem(smolder)->UpdateEmitter(thermo.smokeEmitterId, smolder, 20.0f);
                }

                thermo.storedOriginalGeometry = render.geometry;
                thermo.storedOriginalTransform = transform.matrix;

                if (scene.dustGeometryPrototype) {
                    render.geometry = scene.dustGeometryPrototype;
                }
                render.texturePath = scene.sootTexturePath;

                transform.matrix = glm::translate(glm::mat4(1.0f), basePos);
                transform.matrix = glm::scale(transform.matrix, glm::vec3(0.003f));

                thermo.regrowTimer = 0.0f;
                thermo.burnFactor = 0.0f;
            }
            break;
        }

        case ObjectState::BURNT:
        case ObjectState::REGROWING: {
            const float changeRate = 0.5f * deltaTime;
            const float lerpFactor = glm::clamp(changeRate, 0.0f, 1.0f);
            thermo.currentTemp = glm::mix(thermo.currentTemp, env.weatherIntensity, lerpFactor);

            float growthMultiplier = 0.0f;
            if (env.weatherIntensity > 10.0f) {
                growthMultiplier = (env.weatherIntensity - 10.0f) / 15.0f;
            }
            thermo.regrowTimer += deltaTime * growthMultiplier;

            if (thermo.state == ObjectState::BURNT && thermo.regrowTimer > 5.0f && thermo.smokeEmitterId != -1) {
                scene.GetOrCreateSystem(ParticleLibrary::GetSmokeProps())->StopEmitter(thermo.smokeEmitterId);
                thermo.smokeEmitterId = -1;
            }

            if (thermo.state == ObjectState::BURNT) {
                if (thermo.regrowTimer >= 10.0f) {
                    thermo.state = ObjectState::REGROWING;
                    thermo.regrowTimer = 0.0f;
                    thermo.currentTemp = env.weatherIntensity;

                    if (thermo.storedOriginalGeometry) {
                        render.geometry = thermo.storedOriginalGeometry;
                        thermo.storedOriginalGeometry = nullptr;
                    }
                    render.texturePath = render.originalTexturePath;
                }
            }
            else if (thermo.state == ObjectState::REGROWING) {
                const float growthTime = env.timeConfig.dayLengthSeconds * 0.75f;

                float t = glm::clamp(thermo.regrowTimer / growthTime, 0.0f, 1.0f);
                t = t * t * (3.0f - 2.0f * t);

                const float currentScale = glm::mix(0.003f, 1.0f, t);
                transform.matrix = glm::scale(thermo.storedOriginalTransform, glm::vec3(currentScale));

                if (t >= 1.0f) {
                    thermo.state = ObjectState::NORMAL;
                    thermo.currentTemp = env.weatherIntensity;
                }
            }
            break;
        }
        }
    }
}