#include "ParticleUpdateSystem.h"
#include "../rendering/Scene.h"
#include "../rendering/ParticleLibrary.h"

void ParticleUpdateSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    // 1. Update ECS-driven particle effects (like the moving Dust Cloud)
    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<DustCloudComponent>(e)) continue;

        auto& dust = registry.GetComponent<DustCloudComponent>(e);

        if (dust.isActive) {
            // Move the dust cloud
            dust.position += dust.direction * dust.speed * deltaTime;

            // Update the underlying Vulkan particle emitter
            if (dust.emitterId != -1) {
                ParticleProps props = ParticleLibrary::GetDustStormProps();
                props.position = dust.position;
                scene.GetOrCreateSystem(props)->UpdateEmitter(dust.emitterId, props, 500.0f);
            }

            // If it drifts too far away, stop it
            if (glm::length(dust.position) > 150.0f) {
                scene.StopDust(); // We'll update Scene::StopDust to turn off the component
            }
        }
    }

    // 2. Tick all the underlying Vulkan particle system buffers
    for (const auto& sys : scene.GetParticleSystems()) {
        sys->Update(deltaTime);
    }
}