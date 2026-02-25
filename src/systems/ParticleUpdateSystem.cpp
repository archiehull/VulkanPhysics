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
            dust.position += dust.direction * dust.speed * deltaTime;

            if (dust.emitterId != -1) {
                ParticleProps props = ParticleLibrary::GetDustStormProps();
                props.position = dust.position;
                scene.GetOrCreateSystem(props)->UpdateEmitter(dust.emitterId, props, 500.0f);
            }

            if (glm::length(dust.position) > 150.0f) {
                scene.StopDust();
            }
        }
    }

    // --- NEW: Sync Attached Custom Emitters ---
    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<AttachedEmitterComponent>(e) || !registry.HasComponent<TransformComponent>(e)) continue;

        auto& attached = registry.GetComponent<AttachedEmitterComponent>(e);
        auto& transform = registry.GetComponent<TransformComponent>(e);

        // Iterate backwards or use an iterator so we can safely delete expired emitters
        for (auto it = attached.emitters.begin(); it != attached.emitters.end(); ) {
            auto& activeEm = *it;

            // 1. Check Duration / Timers
            if (activeEm.duration > 0.0f) {
                activeEm.timer += deltaTime;
                if (activeEm.timer >= activeEm.duration) {
                    // Timer expired! Stop the Vulkan emitter and remove it from the list
                    if (activeEm.emitterId != -1) {
                        scene.GetOrCreateSystem(activeEm.props)->StopEmitter(activeEm.emitterId);
                    }
                    it = attached.emitters.erase(it);
                    continue;
                }
            }

            // 2. Sync Position
            if (activeEm.emitterId != -1) {
                ParticleProps props = activeEm.props;

                // Lock the emitter position to the object's transform
                props.position = glm::vec3(transform.matrix[3]);
                if (registry.HasComponent<ColliderComponent>(e)) {
                    props.position.y += registry.GetComponent<ColliderComponent>(e).height * 0.5f;
                }

                scene.GetOrCreateSystem(props)->UpdateEmitter(activeEm.emitterId, props, activeEm.emissionRate);
            }
            ++it;
        }
    }

    // 2. Tick all the underlying Vulkan particle system buffers
    for (const auto& sys : scene.GetParticleSystems()) {
        sys->Update(deltaTime);
    }
}