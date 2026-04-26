#include "SmokeGrenadeSystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include "../rendering/ParticleLibrary.h"
#include <vector>

void SmokeGrenadeSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();
    auto grenadeArray = registry.GetComponentArray<SmokeGrenadeComponent>();
    
    std::vector<Entity> entitiesToDespawn;

    for (Entity i = 0; i < registry.GetEntityCount(); ++i) {
        if (grenadeArray->HasData(i)) {
            auto& grenade = grenadeArray->GetData(i);

            grenade.timer += deltaTime;

            // State 1: Delay before smoke
            if (grenade.timer >= grenade.delayBeforeSmoke && grenade.timer < (grenade.delayBeforeSmoke + grenade.smokeDuration)) {
                if (!grenade.isEmitting) {
                    grenade.isEmitting = true;
                    
                    // Attach smoke emitter
                    ActiveEmitter newEm;
                    newEm.duration = grenade.smokeDuration; // Emit for this long
                    newEm.props = ParticleLibrary::GetSmokeProps();
                    // Optional: Boost emission rate or tweak properties for the grenade
                    newEm.emissionRate = 250.0f; 
                    newEm.props.colorBegin = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f); // Bright white/grey smoke
                    newEm.props.colorEnd = glm::vec4(0.9f, 0.9f, 0.9f, 0.0f);
                    newEm.props.lifeTime = 6.0f; // Smoke lingers
                    newEm.props.sizeBegin = 0.5f;
                    newEm.props.sizeEnd = 4.0f;
                    
                    if (!registry.HasComponent<AttachedEmitterComponent>(i)) {
                        registry.AddComponent<AttachedEmitterComponent>(i, AttachedEmitterComponent{});
                    }
                    registry.GetComponent<AttachedEmitterComponent>(i).emitters.push_back(newEm);
                }
            } 
            
            // Sync ID for UI visibility
            if (grenade.isEmitting && grenade.smokeEmitterId == -1) {
                if (registry.HasComponent<AttachedEmitterComponent>(i)) {
                    auto& attached = registry.GetComponent<AttachedEmitterComponent>(i);
                    if (!attached.emitters.empty()) {
                        // The last one added by this system
                        grenade.smokeEmitterId = attached.emitters.back().emitterId;
                    }
                }
            }            // State 2: Done emitting
            else if (grenade.timer >= (grenade.delayBeforeSmoke + grenade.smokeDuration)) {
                grenade.isEmitting = false;
                // Leave the physical object for a few more seconds before despawning
                if (grenade.timer >= (grenade.delayBeforeSmoke + grenade.smokeDuration + 8.0f)) {
                    entitiesToDespawn.push_back(i);
                }
            }
        }
    }

    for (Entity e : entitiesToDespawn) {
        if (!scene.IsLookaheadMode()) {
            scene.DeleteEntity(e);
        } else {
            scene.DeactivateEntityForLookahead(e);
        }
    }
}
