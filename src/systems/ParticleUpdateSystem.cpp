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

        if (attached.isActive && attached.emitterId != -1) {
            ParticleProps props;
            float rate = 100.0f;

            // Map the effect type to the correct particle properties
            if (attached.effectType == 0) {
                props = ParticleLibrary::GetSmokeProps();
                rate = 50.0f;
            }
            else if (attached.effectType == 1) {
                props = ParticleLibrary::GetFireProps();
                rate = 200.0f;
            }
            else {
                props = ParticleLibrary::GetDustProps();
                props.velocityVariation = glm::vec3(2.0f); // Tighter spread so it sticks to the object
                rate = 150.0f;
            }

            // Lock the emitter position to the object's transform!
            props.position = glm::vec3(transform.matrix[3]);

            // Add a slight vertical offset based on object size if it has a collider
            if (registry.HasComponent<ColliderComponent>(e)) {
                props.position.y += registry.GetComponent<ColliderComponent>(e).height * 0.5f;
            }

            scene.GetOrCreateSystem(props)->UpdateEmitter(attached.emitterId, props, rate);
        }
    }

    // 2. Tick all the underlying Vulkan particle system buffers
    for (const auto& sys : scene.GetParticleSystems()) {
        sys->Update(deltaTime);
    }
}