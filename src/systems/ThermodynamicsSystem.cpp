#include "ThermodynamicsSystem.h"
#include "../rendering/Scene.h"
#include "../rendering/ParticleLibrary.h"
#include <random>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

void ThermodynamicsSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();
    Entity envEntity = scene.GetEnvironmentEntity();

    // FIX 1: Bulletproof Environment Fallback! 
    // If scene logic accidentally wipes the environment, use a default so fire systems never abort.
    EnvironmentComponent fallbackEnv;
    EnvironmentComponent& env = (envEntity != MAX_ENTITIES)
        ? registry.GetComponent<EnvironmentComponent>(envEntity)
        : fallbackEnv;

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        // FIX 2: Remove RenderComponent requirement! (Allows invisible colliders to burn)
        if (!registry.HasComponent<ThermoComponent>(e) ||
            !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        auto& thermo = registry.GetComponent<ThermoComponent>(e);
        if (!thermo.isFlammable) continue;

        auto& transform = registry.GetComponent<TransformComponent>(e);

        // Safely extract render component ONLY if it exists
        RenderComponent* render = registry.HasComponent<RenderComponent>(e)
            ? &registry.GetComponent<RenderComponent>(e)
            : nullptr;

        const glm::vec3 basePos = glm::vec3(transform.matrix[3]);

        switch (thermo.state) {
        case ObjectState::NORMAL:
        case ObjectState::HEATING: {
            const float responseSpeed = thermo.thermalResponse;
            const float effectiveIgnitionThreshold = thermo.ignitionThreshold;
            float targetTemp = env.weatherIntensity;

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

            // Extract true world scale
            const float scaleX = glm::length(glm::vec3(transform.matrix[0]));
            const float scaleY = glm::length(glm::vec3(transform.matrix[1]));
            const float scaleZ = glm::length(glm::vec3(transform.matrix[2]));
            const float maxWorldScale = std::max({ scaleX, scaleY, scaleZ });

            float objectSize = maxWorldScale;
            if (registry.HasComponent<ColliderComponent>(e)) {
                auto& collider = registry.GetComponent<ColliderComponent>(e);
                objectSize = std::max(collider.radius, collider.height * 0.5f) * maxWorldScale;
            }

            // Clamp so we don't spawn millions of particles for a mountain, or 0 for a pebble
            objectSize = glm::clamp(objectSize, 0.5f, 5.0f);

            const float maxFireHeight = 1.5f * objectSize;
            const float minFireHeight = 0.2f * objectSize;
            const float currentFireHeight = minFireHeight + (maxFireHeight - minFireHeight) * growth;

            if (thermo.fireEmitterId != -1) {
                ParticleProps fireProps = ParticleLibrary::GetFireProps();
                fireProps.position = basePos;
                fireProps.position.y += currentFireHeight * 0.5f;

                // 1. SPREAD: engulf the size of the object
                fireProps.positionVariation = glm::vec3(0.4f * objectSize, currentFireHeight * 0.4f, 0.4f * objectSize);

                // 2. SPRITE SIZE: ONLY use temporal growth. DO NOT multiply by objectSize!
                // This guarantees the sprites are always clearly visible (starts at 0.1, grows to 1.5x normal size)
                const float particleScale = 0.1f + (growth * 1.4f);

                fireProps.sizeBegin *= particleScale;
                fireProps.sizeEnd *= particleScale;
                fireProps.sizeVariation *= particleScale;

                // Allow flames to rise a bit faster on large objects so they don't clump
                fireProps.velocity *= particleScale * (1.0f + (objectSize * 0.2f));
                fireProps.velocityVariation *= particleScale;

                // 3. RATE: Multiply rate by objectSize so huge objects pump out LOTS of normal-sized flames
                const float rate = (50.0f + (300.0f * growth)) * objectSize;
                scene.GetOrCreateSystem(fireProps)->UpdateEmitter(thermo.fireEmitterId, fireProps, rate);
            }

            if (thermo.smokeEmitterId != -1) {
                ParticleProps smokeProps = ParticleLibrary::GetSmokeProps();
                smokeProps.position = basePos;
                smokeProps.position.y += currentFireHeight;

                // Same rule for smoke: temporal scale only
                const float smokeScale = 0.1f + (growth * 1.9f);
                smokeProps.sizeBegin *= smokeScale;
                smokeProps.sizeEnd *= smokeScale;
                smokeProps.sizeVariation *= smokeScale;

                smokeProps.velocity *= smokeScale * (1.0f + (objectSize * 0.2f));
                smokeProps.velocityVariation *= smokeScale;
                smokeProps.lifeTime = 6.0f;

                const float rate = (20.0f + (80.0f * growth)) * objectSize;
                scene.GetOrCreateSystem(smokeProps)->UpdateEmitter(thermo.smokeEmitterId, smokeProps, rate);
            }

            glm::vec3 lightPos = basePos;
            lightPos.y += currentFireHeight * 0.5f;

            // FIX 3: Bulletproof ECS Reallocation Strategy
            if (thermo.fireLightEntity == -1 || thermo.fireLightEntity == MAX_ENTITIES) {
                std::string lightName = "FireLight_" + std::to_string(e);
                int newLight = scene.AddLight(lightName, lightPos, glm::vec3(1.0f, 0.5f, 0.1f), 0.0f, 1);

                // Adding a light resizes the ECS vectors! This turns `thermo` into a dangerous dangling pointer!
                // Update the real memory immediately, then BREAK out of the switch!
                // By breaking, we skip the rest of this entity's frame and safely fetch fresh references on the next frame!
                registry.GetComponent<ThermoComponent>(e).fireLightEntity = newLight;
                break;
            }

            // Since we break on creation, it is mathematically guaranteed that `thermo` here is 100% memory safe.
            if (registry.HasComponent<LightComponent>(thermo.fireLightEntity)) {
                auto& fireLightTransform = registry.GetComponent<TransformComponent>(thermo.fireLightEntity);
                auto& fireLightComp = registry.GetComponent<LightComponent>(thermo.fireLightEntity);

                const float t = thermo.burnTimer;
                const float flicker = 1.0f + 0.3f * std::sin(t * 15.0f) + 0.15f * std::sin(t * 37.0f);
                const float targetIntensity = 50.05f * growth * objectSize;

                fireLightTransform.matrix[3] = glm::vec4(lightPos, 1.0f);
                fireLightComp.intensity = targetIntensity * flicker;
            }

            // Burnout logic
            if (thermo.burnTimer >= thermo.maxBurnDuration) {
                if (thermo.canBurnout) {
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

                    // Only interact with render if the object actually has a renderer
                    if (render) {
                        thermo.storedOriginalGeometry = render->geometry;
                        if (scene.dustGeometryPrototype) {
                            render->geometry = scene.dustGeometryPrototype;
                        }
                        render->texturePath = scene.sootTexturePath;
                    }

                    transform.matrix = glm::translate(glm::mat4(1.0f), basePos);
                    transform.matrix = glm::scale(transform.matrix, glm::vec3(0.003f));

                    thermo.regrowTimer = 0.0f;
                    thermo.burnFactor = 0.0f;
                }
                else {
                    thermo.burnTimer = thermo.maxBurnDuration;
                }
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

                    if (render) {
                        if (thermo.storedOriginalGeometry) {
                            render->geometry = thermo.storedOriginalGeometry;
                            thermo.storedOriginalGeometry = nullptr;
                        }
                        render->texturePath = render->originalTexturePath;
                    }
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