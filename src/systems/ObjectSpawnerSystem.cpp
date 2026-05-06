#include "ObjectSpawnerSystem.h"
#include "../rendering/Scene.h"
#include "../systems/PhysicsSystem.h"
#include "../core/Components.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace {
    std::vector<Entity> g_TimedSpawnedEntities;
    constexpr bool kSpawnerDebug = false;
    // Set to true to see per-shot authority/ownership decisions in the console.
    // Flip back to false once the firing/ownership bugs are resolved.
    constexpr bool kSpawnerFireDebug = false;

    char NormalizeSpawnerGroup(const std::string& group) {
        if (group.empty()) return 'A';
        char g = static_cast<char>(std::toupper(static_cast<unsigned char>(group[0])));
        if (g < 'A' || g > 'D') {
            g = 'A';
        }
        return g;
    }

    char NormalizeSpawnerGroup(char group) {
        char g = static_cast<char>(std::toupper(static_cast<unsigned char>(group)));
        if (g < 'A' || g > 'D') {
            g = 'A';
        }
        return g;
    }

    bool IsValidSpawner(Scene& scene, Entity entity) {
        auto& registry = scene.GetRegistry();
        return registry.HasComponent<ObjectSpawnerComponent>(entity) &&
            registry.HasComponent<TransformComponent>(entity);
    }
}

ObjectSpawnerSystem::SpawnCallback ObjectSpawnerSystem::onObjectSpawned = nullptr;

void ObjectSpawnerSystem::ResetSpawnerRun(ObjectSpawnerComponent& spawner) {
    spawner.spawnTimer = 0.0f;
    spawner.runElapsedSeconds = 0.0f;
    spawner.spawnedThisRun = 0;
}

void ObjectSpawnerSystem::Update(Scene& scene, float deltaTime) {
    if (deltaTime <= 0.0f) return;

    auto& registry = scene.GetRegistry();
    std::vector<Entity> expiredSpawnedEntities;

    auto spawnedFromArray = registry.GetComponentArray<SpawnedFromSpawnerComponent>();
    auto despawnerArray = registry.GetComponentArray<DespawnerComponent>();
    auto spawnerArray = registry.GetComponentArray<ObjectSpawnerComponent>();
    auto transformArray = registry.GetComponentArray<TransformComponent>();

    const int localId = PhysicsSystem::localPeerId;
    if (kSpawnerDebug && localId == -1 && scene.IsLookaheadMode()) {
        static bool loggedMissingLocalId = false;
        if (!loggedMissingLocalId) {
            int spawnerCount = 0;
            const Entity count = registry.GetEntityCount();
            for (Entity e = 0; e < count; ++e) {
                if (spawnerArray->HasData(e)) {
                    ++spawnerCount;
                }
            }
            std::cout << "[Spawner] Lookahead active but local peer id is -1. Spawners skipped. Count="
                      << spawnerCount << std::endl;
            loggedMissingLocalId = true;
        }
    }

    if (!g_TimedSpawnedEntities.empty()) {
        const Entity currentCount = registry.GetEntityCount();
        g_TimedSpawnedEntities.erase(
            std::remove_if(
                g_TimedSpawnedEntities.begin(),
                g_TimedSpawnedEntities.end(),
                [&](Entity entity) {
                    if (entity == MAX_ENTITIES || entity >= currentCount) {
                        return true;
                    }
                    if (!spawnedFromArray->HasData(entity) || !despawnerArray->HasData(entity)) {
                        return true;
                    }

                    auto& despawner = despawnerArray->GetData(entity);
                    if (!despawner.enabled || despawner.remainingLifetimeSeconds <= 0.0f) {
                        return false;
                    }

                    despawner.remainingLifetimeSeconds -= deltaTime;
                    if (despawner.remainingLifetimeSeconds <= 0.0f) {
                        expiredSpawnedEntities.push_back(entity);
                        return true;
                    }

                    return false;
                }),
            g_TimedSpawnedEntities.end());
    }

    const size_t spawnerCount = spawnerArray->GetSize();
    for (size_t idx = 0; idx < spawnerCount; ++idx) {
        Entity e = spawnerArray->GetEntityAtIndex(idx);

        if (e == MAX_ENTITIES || !transformArray->HasData(e)) {
            continue; 
        }

        auto& spawner = spawnerArray->GetData(e);
        auto& transform = transformArray->GetData(e);

        if (localId == -1) continue; // Waiting for network handshake, don't spawn yet!

        const bool spawnerIsOwned = registry.HasComponent<OwnershipComponent>(e);

        if (spawnerIsOwned) {
            // Explicitly-owned spawner: only the assigned peer runs any of the logic.
            auto& spawnerOwnership = registry.GetComponent<OwnershipComponent>(e);
            if (static_cast<int>(spawnerOwnership.GetOwnerIndex()) != localId) {
                continue;
            }
        }
        // Unowned spawner: all peers tick the timer below so they stay in sync,
        // but only the current autofireAuthority peer actually calls SpawnObjectFromSpawner.

        if (spawner.attachToTarget) {
            Entity targetEnt = MAX_ENTITIES;
            if (spawner.attachTargetName.empty()) {
                auto cameraArray = registry.GetComponentArray<CameraComponent>();
                for (size_t cIdx = 0; cIdx < cameraArray->GetSize(); ++cIdx) {
                    Entity c = cameraArray->GetEntityAtIndex(cIdx);
                    if (c != MAX_ENTITIES && cameraArray->GetData(c).isActive) {
                        targetEnt = c;
                        break;
                    }
                }
            }
            else {
                targetEnt = scene.GetEntityByName(spawner.attachTargetName);
            }

            if (targetEnt != MAX_ENTITIES && registry.HasComponent<TransformComponent>(targetEnt)) {
                auto& targetTransform = registry.GetComponent<TransformComponent>(targetEnt);
                transform.position = targetTransform.position;
                transform.rotation = targetTransform.rotation;
                transform.UpdateMatrix();
            }
        }

        if (spawner.alwaysOn) {
            spawner.isRunning = true;
            spawner.runDurationSeconds = -1.0f;
            spawner.maxSpawnsPerRun = -1;
        }
        if (!spawner.isRunning) continue;

        if (spawner.maxSpawnsPerRun >= 0 && spawner.spawnedThisRun >= spawner.maxSpawnsPerRun) {
            if (!spawner.alwaysOn) spawner.isRunning = false;
            continue;
        }

        spawner.runElapsedSeconds += deltaTime;
        if (!spawner.alwaysOn && spawner.runDurationSeconds > 0.0f && spawner.runElapsedSeconds >= spawner.runDurationSeconds) {
            spawner.isRunning = false;
            continue;
        }

        const float interval = std::max(0.01f, spawner.spawnInterval);
        spawner.spawnTimer += deltaTime;

        constexpr int kMaxCatchUpSpawnsPerFrame = 4;
        int spawnsThisFrame = 0;

        while (spawner.spawnTimer >= interval && spawnsThisFrame < kMaxCatchUpSpawnsPerFrame) {
            if (spawner.maxSpawnsPerRun >= 0 && spawner.spawnedThisRun >= spawner.maxSpawnsPerRun) {
                if (!spawner.alwaysOn) spawner.isRunning = false;
                break;
            }

            if (!spawner.alwaysOn && spawner.runDurationSeconds > 0.0f && spawner.runElapsedSeconds >= spawner.runDurationSeconds) {
                spawner.isRunning = false;
                break;
            }

            spawner.spawnTimer -= interval;

            if (kSpawnerFireDebug) {
                std::string dbgName = registry.HasComponent<NameComponent>(e)
                    ? registry.GetComponent<NameComponent>(e).name : "?";
                std::cout << "[SpawnerFire] Spawner='" << dbgName << "'"
                    << " entity=" << e
                    << " localId=" << localId
                    << " owned=" << spawnerIsOwned
                    << " autofireAuth=" << (int)spawner.autofireAuthority
                    << " rotateAuth=" << spawner.rotateAuthority
                    << " alwaysOn=" << spawner.alwaysOn
                    << " assignedOwner=" << (int)spawner.assignedOwner
                    << " nextSeqOwner=" << (int)spawner.nextSequentialOwner
                    << " activePeers=["
                    << PhysicsSystem::activePeers[0] << PhysicsSystem::activePeers[1]
                    << PhysicsSystem::activePeers[2] << PhysicsSystem::activePeers[3] << "]"
                    << std::endl;
            }

            // For unowned spawners, only the authority peer fires to avoid duplicates.
            // All peers run this loop so their timers and authority counters stay in sync.
            if (!spawnerIsOwned) {
                const bool willFire = (localId == static_cast<int>(spawner.autofireAuthority));
                if (kSpawnerFireDebug) {
                    std::cout << "[SpawnerFire]   Unowned: localId=" << localId
                        << " auth=" << (int)spawner.autofireAuthority
                        << " -> " << (willFire ? "FIRE" : "skip") << std::endl;
                }
                if (willFire) {
                    SpawnObjectFromSpawner(scene, e);
                }
                // Always advance sequential owner before any early-exit so the
                // counter stays in sync regardless of whether rotateAuthority is on.
                if (spawner.assignedOwner == ObjectSpawnerComponent::OWNER_SEQUENTIAL) {
                    const uint8_t oldSeq = spawner.nextSequentialOwner;
                    for (int i = 0; i < 4; ++i) {
                        spawner.nextSequentialOwner = (spawner.nextSequentialOwner + 1) % 4;
                        if (PhysicsSystem::activePeers[spawner.nextSequentialOwner]) break;
                    }
                    if (kSpawnerFireDebug) {
                        std::cout << "[SpawnerFire]   SeqOwner (unowned): " << (int)oldSeq
                            << " -> " << (int)spawner.nextSequentialOwner << std::endl;
                    }
                }
                if (spawner.rotateAuthority) {
                    const uint8_t oldAuth = spawner.autofireAuthority;
                    for (int i = 0; i < 4; ++i) {
                        spawner.autofireAuthority = (spawner.autofireAuthority + 1) % 4;
                        if (PhysicsSystem::activePeers[spawner.autofireAuthority]) break;
                    }
                    if (kSpawnerFireDebug) {
                        std::cout << "[SpawnerFire]   RotateAuth: " << (int)oldAuth
                            << " -> " << (int)spawner.autofireAuthority << std::endl;
                    }
                    // Preserve leftover timer to avoid cumulative delay/jitter across rotations,
                    // while the break below still prevents burst spawns in a single frame.
                    break;
                }
            }
            else {
                if (kSpawnerFireDebug) {
                    std::cout << "[SpawnerFire]   Owned: p" << localId << " fires (owner always fires)" << std::endl;
                }
                SpawnObjectFromSpawner(scene, e);
                // NEW: Advance sequential owner in lockstep for the owner
                if (spawner.assignedOwner == ObjectSpawnerComponent::OWNER_SEQUENTIAL) {
                    const uint8_t oldSeq = spawner.nextSequentialOwner;
                    for (int i = 0; i < 4; ++i) {
                        spawner.nextSequentialOwner = (spawner.nextSequentialOwner + 1) % 4;
                        if (PhysicsSystem::activePeers[spawner.nextSequentialOwner]) break;
                    }
                    if (kSpawnerFireDebug) {
                        std::cout << "[SpawnerFire]   SeqOwner (owned): " << (int)oldSeq
                            << " -> " << (int)spawner.nextSequentialOwner << std::endl;
                    }
                }
            }
            ++spawnsThisFrame;
        }

        if (spawner.spawnTimer > interval) {
            // Prevent unbounded backlog: keep at most one interval of debt to smooth spawns over frames.
            spawner.spawnTimer = std::fmod(spawner.spawnTimer, interval);
        }
    }

    for (Entity expired : expiredSpawnedEntities) {
        if (expired < registry.GetEntityCount() && spawnedFromArray->HasData(expired)) {
            if (scene.IsLookaheadMode()) {
                scene.DeactivateEntityForLookahead(expired);
            } else {
                scene.DeleteEntity(expired);
            }
        }
    }
}

void ObjectSpawnerSystem::FireOnce(Scene& scene, Entity spawnerEntity) {
    if (!IsValidSpawner(scene, spawnerEntity)) return;

    int localId = PhysicsSystem::localPeerId;
    auto& registry = scene.GetRegistry();

    if (localId != -1) {
        if (registry.HasComponent<OwnershipComponent>(spawnerEntity)) {
            if (static_cast<int>(registry.GetComponent<OwnershipComponent>(spawnerEntity).GetOwnerIndex()) != localId) return;
        }
        // Unowned spawners may be fired by any peer
    }

    SpawnObjectFromSpawner(scene, spawnerEntity);

    // --- NEW: Advance sequential owner in lockstep for manual fires ---
    auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(spawnerEntity);
    if (spawner.assignedOwner == ObjectSpawnerComponent::OWNER_SEQUENTIAL) {
        for (int i = 0; i < 4; ++i) {
            spawner.nextSequentialOwner = (spawner.nextSequentialOwner + 1) % 4;
            if (PhysicsSystem::activePeers[spawner.nextSequentialOwner]) {
                break;
            }
        }
    }
}

void ObjectSpawnerSystem::FireGroup(Scene& scene, const std::string& group) {
    auto& registry = scene.GetRegistry();
    const Entity count = registry.GetEntityCount();
    const char normalizedGroup = NormalizeSpawnerGroup(group);

    for (Entity e = 0; e < count; ++e) {
        if (!registry.HasComponent<ObjectSpawnerComponent>(e) ||
            !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);

        if (NormalizeSpawnerGroup(spawner.group) != normalizedGroup) {
            continue;
        }

        int localId = PhysicsSystem::localPeerId;
        if (localId != -1) {
            if (registry.HasComponent<OwnershipComponent>(e)) {
                if (static_cast<int>(registry.GetComponent<OwnershipComponent>(e).GetOwnerIndex()) != localId) continue;
            }
            // Unowned spawners may be fired by any peer
        }

        SpawnObjectFromSpawner(scene, e);

        // --- NEW: Advance sequential owner in lockstep for manual fires ---
        if (spawner.assignedOwner == ObjectSpawnerComponent::OWNER_SEQUENTIAL) {
            for (int i = 0; i < 4; ++i) {
                spawner.nextSequentialOwner = (spawner.nextSequentialOwner + 1) % 4;
                if (PhysicsSystem::activePeers[spawner.nextSequentialOwner]) {
                    break;
                }
            }
        }
    }
}

void ObjectSpawnerSystem::StartSpawner(Scene& scene, Entity spawnerEntity) {
    if (!IsValidSpawner(scene, spawnerEntity)) {
        return;
    }

    auto& spawner = scene.GetRegistry().GetComponent<ObjectSpawnerComponent>(spawnerEntity);
    ResetSpawnerRun(spawner);
    spawner.isRunning = true;
}

void ObjectSpawnerSystem::StartGroup(Scene& scene, const std::string& group) {
    auto& registry = scene.GetRegistry();
    const Entity count = registry.GetEntityCount();
    const char normalizedGroup = NormalizeSpawnerGroup(group);

    for (Entity e = 0; e < count; ++e) {
        if (!IsValidSpawner(scene, e)) {
            continue;
        }

        auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
        if (NormalizeSpawnerGroup(spawner.group) != normalizedGroup) {
            continue;
        }

        StartSpawner(scene, e);
    }
}

void ObjectSpawnerSystem::TriggerStartupSpawners(Scene& scene) {
    auto& registry = scene.GetRegistry();
    const Entity count = registry.GetEntityCount();

    for (Entity e = 0; e < count; ++e) {
        if (!IsValidSpawner(scene, e)) {
            continue;
        }

        auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
        if (!spawner.triggerOnStartup && !spawner.alwaysOn) {
            continue;
        }

        StartSpawner(scene, e);
    }
}

void ObjectSpawnerSystem::SpawnObjectFromSpawner(Scene& scene, Entity spawnerEntity) {
    auto& registry = scene.GetRegistry();
    if (!registry.HasComponent<ObjectSpawnerComponent>(spawnerEntity) ||
        !registry.HasComponent<TransformComponent>(spawnerEntity)) {
        return;
    }

    auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(spawnerEntity);
    const auto& transform = registry.GetComponent<TransformComponent>(spawnerEntity);

    std::string spawnerName = "Spawner";
    if (registry.HasComponent<NameComponent>(spawnerEntity)) {
        std::string name = registry.GetComponent<NameComponent>(spawnerEntity).name;
        if (!name.empty()) {
            spawnerName = name;
        }
    }

    const std::string& geometryType = spawner.spawnGeometryType;
    const std::string spawnedName = spawnerName + "_" + geometryType + "_Spawned_" + std::to_string(spawner.spawnedCount++);
    spawner.spawnedThisRun++;
    glm::vec3 spawnPos = transform.position;
    if (spawner.attachToTarget) {
        // Offset forward slightly so objects don't spawn exactly inside the camera/target
        glm::vec3 front = -glm::normalize(glm::vec3(transform.matrix[2]));
        spawnPos += front * 2.5f; 
    }
    
    // --- NEW: Apply Random Area Spawning ---
    if (spawner.randomizePosition) {
        static std::mt19937 rng_pos(std::random_device{}());
        std::uniform_real_distribution<float> distX(spawner.randomPosMin.x, spawner.randomPosMax.x);
        std::uniform_real_distribution<float> distY(spawner.randomPosMin.y, spawner.randomPosMax.y);
        std::uniform_real_distribution<float> distZ(spawner.randomPosMin.z, spawner.randomPosMax.z);

        // Treat min and max as local offsets from the spawner's base position
        spawnPos.x += distX(rng_pos);
        spawnPos.y += distY(rng_pos);
        spawnPos.z += distZ(rng_pos);
    }
    
    glm::vec3 effectiveSpawnScale = spawner.spawnScale;
    if (spawner.randomizeScale) {
        static std::mt19937 rng_scale(std::random_device{}());
        std::uniform_real_distribution<float> sX(spawner.scaleMin.x, spawner.scaleMax.x);
        
        if (geometryType == "Sphere") {
            // For spheres, we force uniform scaling by using the same random value for all axes
            float uniformScale = sX(rng_scale);
            effectiveSpawnScale = glm::vec3(uniformScale);
        } else {
            std::uniform_real_distribution<float> sY(spawner.scaleMin.y, spawner.scaleMax.y);
            std::uniform_real_distribution<float> sZ(spawner.scaleMin.z, spawner.scaleMax.z);
            effectiveSpawnScale.x = sX(rng_scale);
            effectiveSpawnScale.y = sY(rng_scale);
            effectiveSpawnScale.z = sZ(rng_scale);
        }
    }

    float sphereBaseRadius = 0.5f;
    if (geometryType == "Sphere") {
        const float uniform = std::max(0.05f, spawner.spawnObjectScale);
        const glm::vec3 axisScale(
            std::max(0.05f, effectiveSpawnScale.x),
            std::max(0.05f, effectiveSpawnScale.y),
            std::max(0.05f, effectiveSpawnScale.z));

        sphereBaseRadius = std::max(0.1f, 0.5f * uniform);
        // Final sphere world extents = base radius (uniform) * axis scale (XYZ)
        effectiveSpawnScale = axisScale * uniform;
    }

    Entity spawnedEntity = MAX_ENTITIES;
    if (geometryType == "Cube") {
        spawnedEntity = scene.AddCube(spawnedName, spawnPos, effectiveSpawnScale, spawner.spawnTexturePath);
    }
    else if (geometryType == "Plane") {
        spawnedEntity = scene.AddPlane(spawnedName, spawnPos, effectiveSpawnScale, spawner.spawnTexturePath);
    }
    else if (geometryType == "Model" && !spawner.spawnModelPath.empty()) {
        spawnedEntity = scene.AddModel(spawnedName, spawnPos, glm::vec3(0.0f), effectiveSpawnScale, spawner.spawnModelPath, spawner.spawnTexturePath, false);
    }
    else if (geometryType == "Capsule") {
        // Base unit capsule: radius 0.2, total height 1.0 (longer and thinner default)
        float r = 0.2f;
        float h = 1.0f;
        spawnedEntity = scene.AddCapsule(spawnedName, r, h, 32, 16, spawnPos, effectiveSpawnScale, spawner.spawnTexturePath);
    }
    else if (geometryType == "Smoke Grenade") {
        // Base Unit Capsule scale for Smoke Grenade (radius 0.15, total height 0.75 - M18 canister proportions)
        spawnedEntity = scene.AddCapsule(spawnedName, 0.15f, 0.75f, 24, 12, spawnPos, effectiveSpawnScale, "textures/smoke_grenade.png");
        
        if (spawnedEntity != MAX_ENTITIES) {
            registry.AddComponent<SmokeGrenadeComponent>(spawnedEntity, SmokeGrenadeComponent{});
        }
    }
    else {
        spawnedEntity = scene.AddSphere(spawnedName, 16, 32, spawnPos, effectiveSpawnScale, spawner.spawnTexturePath);
    }

    if (spawnedEntity == MAX_ENTITIES) return;

    registry.AddComponent<SpawnedFromSpawnerComponent>(spawnedEntity, { spawnerEntity });
    if (spawner.spawnLifespanSeconds > 0.0f) {
        if (registry.HasComponent<DespawnerComponent>(spawnedEntity)) {
            auto& despawner = registry.GetComponent<DespawnerComponent>(spawnedEntity);
            despawner.enabled = true;
            despawner.remainingLifetimeSeconds = spawner.spawnLifespanSeconds;
        }
        else {
            registry.AddComponent<DespawnerComponent>(spawnedEntity, { true, spawner.spawnLifespanSeconds });
        }
        g_TimedSpawnedEntities.push_back(spawnedEntity);
    }

    if (!registry.HasComponent<PhysicsComponent>(spawnedEntity)) {
        registry.AddComponent<PhysicsComponent>(spawnedEntity, PhysicsComponent{});
    }
    if (!registry.HasComponent<ColliderComponent>(spawnedEntity)) {
        registry.AddComponent<ColliderComponent>(spawnedEntity, ColliderComponent{});
    }

    auto& collider = registry.GetComponent<ColliderComponent>(spawnedEntity);
    collider.hasCollision = true;
    
    if (geometryType == "Smoke Grenade" || geometryType == "Capsule") {
        collider.type = 2; // Capsule
        if (geometryType == "Smoke Grenade") {
            // MATCH THE EXACT DIMENSIONS FROM AddCapsule()
            collider.radius = 0.15f * std::max({ effectiveSpawnScale.x, effectiveSpawnScale.z });
            collider.height = 0.75f * effectiveSpawnScale.y;
        }
        else {
            // Matches the generic Capsule proportions
            collider.radius = 0.2f * std::max({ effectiveSpawnScale.x, effectiveSpawnScale.z });
            collider.height = 1.0f * effectiveSpawnScale.y;
        }
    }
    else if (geometryType == "Cube") {
        collider.type = 3; // Box/AABB
        collider.halfExtents = effectiveSpawnScale * 0.5f;
        collider.radius = std::max({ effectiveSpawnScale.x, effectiveSpawnScale.y, effectiveSpawnScale.z }) * 0.5f;
    }
    else {
        collider.type = 0; // Sphere
        collider.radius = std::max(0.1f, std::max({ effectiveSpawnScale.x, effectiveSpawnScale.y, effectiveSpawnScale.z }) * 0.5f);
    }
    collider.normal = glm::vec3(0.0f, 1.0f, 0.0f);

    auto& phys = registry.GetComponent<PhysicsComponent>(spawnedEntity);
    phys.isStatic = false;
    phys.SetMass(std::max(0.01f, spawner.spawnMass));
    phys.restitution = spawner.spawnRestitution;
    phys.friction = spawner.spawnFriction;

    if (geometryType == "Cube") {
        phys.SetBoxInertia(effectiveSpawnScale * 0.5f);
    }

    glm::vec3 velocity = spawner.spawnVelocity;
    if (spawner.attachToTarget) {
        // If attached to a target, spawn objects moving in the direction the target (spawner) is looking.
        // We use the matrix's forward vector (-Z).
        glm::vec3 front = -glm::normalize(glm::vec3(transform.matrix[2]));
        float speed = glm::length(spawner.spawnVelocity);
        if (speed < 0.001f) speed = 20.0f; // Default if speed was 0
        velocity = front * speed;
    }

    if (spawner.randomizeVelocity) {
        static std::mt19937 rng_vel(std::random_device{}());
        std::uniform_real_distribution<float> distX(spawner.velocityMin.x, spawner.velocityMax.x);
        std::uniform_real_distribution<float> distY(spawner.velocityMin.y, spawner.velocityMax.y);
        std::uniform_real_distribution<float> distZ(spawner.velocityMin.z, spawner.velocityMax.z);

        velocity.x += distX(rng_vel);
        velocity.y += distY(rng_vel);
        velocity.z += distZ(rng_vel);
    }

    // Prepare angular velocity
    glm::vec3 angVel = spawner.spawnAngularVelocity;
    if (spawner.randomizeAngularVelocity) {
        static std::mt19937 rng_ang(std::random_device{}());
        std::uniform_real_distribution<float> aX(spawner.angularVelocityMin.x, spawner.angularVelocityMax.x);
        std::uniform_real_distribution<float> aY(spawner.angularVelocityMin.y, spawner.angularVelocityMax.y);
        std::uniform_real_distribution<float> aZ(spawner.angularVelocityMin.z, spawner.angularVelocityMax.z);
        angVel.x += aX(rng_ang);
        angVel.y += aY(rng_ang);
        angVel.z += aZ(rng_ang);
    }

    phys.velocity = velocity;
    phys.angularVelocity = angVel; // assign initial spin
    
    // Assign ownership to spawned object
    uint8_t objectOwner = spawner.assignedOwner;
    if (spawner.assignedOwner == ObjectSpawnerComponent::OWNER_SEQUENTIAL) {
        // SEQUENTIAL mode: use nextSequentialOwner (advancement is now handled externally)
        objectOwner = spawner.nextSequentialOwner;
        if (kSpawnerFireDebug) {
            std::cout << "[SpawnerSpawn] SEQUENTIAL -> using nextSeqOwner=" << (int)objectOwner << std::endl;
        }
    }
    else if (spawner.assignedOwner == ObjectSpawnerComponent::OWNER_LOCAL) {
        // LOCAL mode: Use the ID of the peer that is currently running the simulation
        int localId = PhysicsSystem::localPeerId;

        if (localId == -1 && registry.HasComponent<OwnershipComponent>(spawnerEntity)) {
            // If we're still connecting but the spawner already has an assigned owner,
            // use that. This prevents objects from being "orphaned" to Peer 0
            // before the local peer ID handshake completes.
            objectOwner = static_cast<uint8_t>(registry.GetComponent<OwnershipComponent>(spawnerEntity).GetOwnerIndex());
        }
        else {
            objectOwner = static_cast<uint8_t>(localId == -1 ? 0 : localId);
        }
        if (kSpawnerFireDebug) {
            std::cout << "[SpawnerSpawn] LOCAL -> objectOwner=" << (int)objectOwner << std::endl;
        }
    }
    else {
        if (kSpawnerFireDebug) {
            std::cout << "[SpawnerSpawn] FIXED -> objectOwner=" << (int)objectOwner << std::endl;
        }
    }

    ObjectOwnershipType ownerType = static_cast<ObjectOwnershipType>(std::min(objectOwner, (uint8_t)3));
    OwnershipComponent ownComp{ ownerType };
    registry.AddComponent<OwnershipComponent>(spawnedEntity, ownComp);

    if (kSpawnerFireDebug) {
        std::cout << "[SpawnerSpawn] entity=" << spawnedEntity
            << " name=" << spawnedName
            << " finalOwner=" << (int)ownerType
            << " firedBy=p" << PhysicsSystem::localPeerId << std::endl;
    }
    
    //std::cout << "[ObjectSpawnerSystem] Spawned Entity: " << spawnedEntity << " (" << geometryType << ") Owner: " << (int)ownerType << " Name: " << spawnedName << std::endl;

    // --- NEW: Register with Scene for optimized broadcast ---
    if (static_cast<int>(ownerType) == PhysicsSystem::localPeerId) {
        scene.RegisterLocallyOwnedNetworkEntity(spawnedEntity);
    }

    // --- NEW: Apply Owner-specific visual feedback ---
    if (registry.HasComponent<RenderComponent>(spawnedEntity)) {
        auto& render = registry.GetComponent<RenderComponent>(spawnedEntity);
        render.useDebugOverlay = false;
        render.useColorTint = true;
        render.tintColor = ownComp.GetOwnerColor();
        render.tintColor.a = 1.0f;
    }

    if (onObjectSpawned) {
        SpawnEvent ev;
        ev.entityId = spawnedEntity;
        ev.geometryType = geometryType;
        ev.position = spawnPos;
        ev.scale = effectiveSpawnScale;
        ev.texturePath = spawner.spawnTexturePath;
        ev.modelPath = spawner.spawnModelPath;
        ev.mass = phys.mass;
        ev.velocity = phys.velocity;
        ev.angularVelocity = phys.angularVelocity;
        ev.ownerId = objectOwner;
        onObjectSpawned(ev);
    }
}
