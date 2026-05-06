#include "ParticleUpdateSystem.h"
#include "../rendering/Scene.h"
#include "../rendering/ParticleLibrary.h"

void ParticleUpdateSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    auto dustArray = registry.GetComponentArray<DustCloudComponent>();
    const size_t dustCount = dustArray->GetSize();

    // 1. Update ECS-driven particle effects (like the moving Dust Cloud)
    for (size_t idx = 0; idx < dustCount; ++idx) {
        Entity e = dustArray->GetEntityAtIndex(idx);
        if (e == MAX_ENTITIES) continue;

        auto& dust = dustArray->GetData(e);

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
    auto attachedArray = registry.GetComponentArray<AttachedEmitterComponent>();
    auto transformArray = registry.GetComponentArray<TransformComponent>();
    const size_t attachedCount = attachedArray->GetSize();

    for (size_t idx = 0; idx < attachedCount; ++idx) {
        Entity e = attachedArray->GetEntityAtIndex(idx);
        if (e == MAX_ENTITIES || !transformArray->HasData(e)) continue;

        auto& attached = attachedArray->GetData(e);
        auto& transform = transformArray->GetData(e);

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

            // 2. Sync Position & Creation
            ParticleProps props = activeEm.props;

            // Lock the emitter position to the object's transform
            props.position = glm::vec3(transform.matrix[3]);
            auto colliderArray = registry.GetComponentArray<ColliderComponent>();
            if (colliderArray->HasData(e)) {
                props.position.y += colliderArray->GetData(e).height * 0.5f;
            }

            if (activeEm.emitterId == -1) {
                // INITIALIZE: Create the emitter in the Vulkan system
                activeEm.emitterId = scene.GetOrCreateSystem(props)->AddEmitter(props, activeEm.emissionRate);
            } else {
                // UPDATE: Sync properties
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