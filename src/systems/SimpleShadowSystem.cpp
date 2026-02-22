#include "SimpleShadowSystem.h"
#include "../rendering/Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

void SimpleShadowSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();
    Entity envEntity = scene.GetEnvironmentEntity();
    if (envEntity == MAX_ENTITIES) return;

    auto& env = registry.GetComponent<EnvironmentComponent>(envEntity);

    // Only process if enabled
    if (!env.useSimpleShadows) return;

    glm::vec3 lightPos = glm::vec3(0.0f, 100.0f, 0.0f);
    bool sunIsUp = false;

    Entity sunEntity = scene.GetEntityByName("Sun");
    if (sunEntity != MAX_ENTITIES && registry.HasComponent<TransformComponent>(sunEntity)) {
        auto& sunTransform = registry.GetComponent<TransformComponent>(sunEntity);
        lightPos = glm::vec3(sunTransform.matrix[3]);
        if (lightPos.y > -20.0f) sunIsUp = true;
    }

    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<RenderComponent>(e) || !registry.HasComponent<TransformComponent>(e)) continue;

        auto& render = registry.GetComponent<RenderComponent>(e);
        if (render.simpleShadowEntity == MAX_ENTITIES) continue;

        auto& shadowRender = registry.GetComponent<RenderComponent>(render.simpleShadowEntity);
        auto& shadowTransform = registry.GetComponent<TransformComponent>(render.simpleShadowEntity);
        auto& parentTransform = registry.GetComponent<TransformComponent>(e);

        if (sunIsUp && render.visible) {
            const glm::vec3 parentPos = glm::vec3(parentTransform.matrix[3]);
            const glm::vec3 rawLightDir = parentPos + glm::vec3(0.0f, 0.15f, 0.0f) - lightPos;
            const glm::vec3 lightDir3D = glm::normalize(rawLightDir);

            glm::vec3 flatDir = glm::vec3(lightDir3D.x, 0.0f, lightDir3D.z);
            flatDir = (glm::length(flatDir) > 0.001f) ? glm::normalize(flatDir) : glm::vec3(0.0f, 0.0f, 1.0f);

            const float angle = std::atan2(flatDir.x, flatDir.z);
            const float dotY = std::abs(lightDir3D.y);
            float stretch = std::clamp(1.0f + (1.0f - dotY) * 8.0f, 1.0f, 12.0f);

            const float parentScale = glm::length(glm::vec3(parentTransform.matrix[0]));
            const float shadowRadius = std::max(parentScale * 1.5f, 0.5f);
            const float shiftAmount = shadowRadius * (stretch - 1.0f);
            const glm::vec3 finalPos = parentPos + glm::vec3(0.0f, 0.15f, 0.0f) + (flatDir * shiftAmount);

            glm::mat4 m = glm::mat4(1.0f);
            m = glm::translate(m, finalPos);
            m = glm::rotate(m, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::scale(m, glm::vec3(1.0f, 1.0f, stretch));

            shadowTransform.matrix = m;
            shadowRender.visible = true;
        }
        else {
            shadowRender.visible = false;
        }
    }
}