#pragma once

#include "ISystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include "OrbitSystem.h"
#include "PhysicsSystem.h"
#include "ObjectSpawnerSystem.h"
#include "../util/AnimationMath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AnimationSystem : public ISystem {
public:
    inline static float globalPlaybackSpeed = 1.0f;
    inline static bool replayDebugLogging = true;

    static void SetReplayDebugLogging(bool enabled) {
        replayDebugLogging = enabled;
    }

    static void StartRealtimeRecording(Scene& scene) {
        lookaheadSamples.clear();
        lookaheadDurationSeconds = 0.0f;
        realtimeRecordingElapsedSeconds = 0.0f;
        isRealtimeRecording = true;
        lookaheadSamples.push_back(CaptureSceneSample(scene, 0.0f));
    }

    static void StopRealtimeRecording() {
        isRealtimeRecording = false;
    }

    static bool IsRealtimeRecording() {
        return isRealtimeRecording;
    }

    static void ResetReplayState(Scene* scene = nullptr) {
        DebugLog("ResetReplayState begin");
        isRealtimeRecording = false;
        realtimeRecordingElapsedSeconds = 0.0f;
        lookaheadDurationSeconds = 0.0f;
        lookaheadSamples.clear();

        if (scene != nullptr) {
            auto& registry = scene->GetRegistry();
            for (Entity e : hiddenReplayEntities) {
                if (e != MAX_ENTITIES &&
                    e < registry.GetEntityCount() &&
                    registry.HasComponent<RenderComponent>(e) &&
                    !IsReplayGhostEntity(registry, e)) {
                    registry.GetComponent<RenderComponent>(e).visible = true;
                }
            }
            hiddenReplayEntities.clear();

            CleanupReplayGhosts(*scene);
            const Entity cameraModelEntity = scene->GetEntityByName(kReplayCameraModelName);
            if (cameraModelEntity != MAX_ENTITIES && registry.HasComponent<NameComponent>(cameraModelEntity)) {
                scene->DeleteEntity(cameraModelEntity);
            }
        }
        else {
            hiddenReplayEntities.clear();
        }

        replayGhostEntities.clear();
        DebugLog("ResetReplayState complete");
    }

    static void RecordRealtimeFrame(Scene& scene, float deltaTime) {
        if (!isRealtimeRecording) {
            return;
        }

        realtimeRecordingElapsedSeconds += std::max(0.0f, deltaTime);
        lookaheadDurationSeconds = realtimeRecordingElapsedSeconds;
        lookaheadSamples.push_back(CaptureSceneSample(scene, realtimeRecordingElapsedSeconds));
    }

    static void GenerateLookahead(Scene& scene, float durationSeconds, float stepSeconds = (1.0f / 30.0f)) {
        ResetReplayState(&scene);

        const float safeDuration = std::max(0.0f, durationSeconds);
        const float safeStep = std::max(stepSeconds, 0.001f);
        lookaheadDurationSeconds = safeDuration;

        auto simulationScene = scene.CreateSimulationClone();
        if (!simulationScene) {
            DebugLog("GenerateLookahead aborted: failed to create simulation clone");
            return;
        }

        Scene& simScene = *simulationScene;
        auto& registry = simScene.GetRegistry();
        const Entity maxEntityId = registry.GetEntityCount();
        DebugLog("GenerateLookahead begin: duration=" + std::to_string(durationSeconds) +
            " step=" + std::to_string(stepSeconds) +
            " entityCount=" + std::to_string(maxEntityId));

        const int sampleCount = static_cast<int>(std::ceil(safeDuration / safeStep)) + 1;
        lookaheadSamples.reserve(static_cast<size_t>(std::max(sampleCount, 1)));
        DebugLog("GenerateLookahead sampleCount=" + std::to_string(sampleCount) +
            " initialTrackedEntities=" + std::to_string(maxEntityId));

        lookaheadSamples.push_back(CaptureSceneSample(simScene, 0.0f));

        OrbitSystem orbitSystem;
        AnimationSystem animationSystem;
        PhysicsSystem physicsSystem;
        ObjectSpawnerSystem objectSpawnerSystem;
        const bool prevDespawnerDebug = PhysicsSystem::debugDespawnerDeletion;
        PhysicsSystem::SetDebugDespawnerDeletion(replayDebugLogging);

        for (int i = 1; i < sampleCount; ++i) {
            orbitSystem.Update(simScene, safeStep);
            animationSystem.Update(simScene, safeStep);
            physicsSystem.Update(simScene, safeStep);
            objectSpawnerSystem.Update(simScene, safeStep);

            const float t = std::min(static_cast<float>(i) * safeStep, safeDuration);
            lookaheadSamples.push_back(CaptureSceneSample(simScene, t));

            if (replayDebugLogging && ((i % 60) == 0 || i == sampleCount - 1)) {
                DebugLog("Lookahead step=" + std::to_string(i) + "/" + std::to_string(sampleCount - 1) +
                    " simEntityCount=" + std::to_string(registry.GetEntityCount()) +
                    " samples=" + std::to_string(lookaheadSamples.size()));
            }
        }

        PhysicsSystem::SetDebugDespawnerDeletion(prevDespawnerDebug);

        DebugLog("GenerateLookahead complete: finalSimEntityCount=" + std::to_string(registry.GetEntityCount()) +
            " lookaheadSamples=" + std::to_string(lookaheadSamples.size()));
    }

    static bool HasLookahead() {
        return !lookaheadSamples.empty();
    }

    static float GetLookaheadDuration() {
        return lookaheadDurationSeconds;
    }

    static void ScrubLookahead(Scene& scene, float timeSeconds, bool viewRecordedCamera, bool highlightSpawnedObjects = false) {
        if (lookaheadSamples.empty()) {
            return;
        }

        const float clampedTime = glm::clamp(timeSeconds, 0.0f, lookaheadDurationSeconds);
        size_t bestIndex = 0;
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < lookaheadSamples.size(); ++i) {
            const float dist = std::abs(lookaheadSamples[i].timeSeconds - clampedTime);
            if (dist < bestDistance) {
                bestDistance = dist;
                bestIndex = i;
            }
        }

        auto& registry = scene.GetRegistry();
        const auto& sample = lookaheadSamples[bestIndex];

        for (const auto& pair : replayGhostEntities) {
            const Entity ghostEntity = pair.second;
            if (ghostEntity != MAX_ENTITIES && IsReplayGhostEntity(registry, ghostEntity) && registry.HasComponent<RenderComponent>(ghostEntity)) {
                registry.GetComponent<RenderComponent>(ghostEntity).visible = false;
            }
        }

        Entity activeCameraEntity = MAX_ENTITIES;
        const Entity entityCount = registry.GetEntityCount();
        for (Entity e = 0; e < entityCount; ++e) {
            if (registry.HasComponent<CameraComponent>(e) && registry.GetComponent<CameraComponent>(e).isActive) {
                activeCameraEntity = e;
                break;
            }
        }

        for (const auto& transformSample : sample.entityTransforms) {
            const Entity entity = transformSample.entity;
            if (!viewRecordedCamera && entity == activeCameraEntity) {
                continue;
            }
            if (registry.HasComponent<TransformComponent>(entity) &&
                !IsReplayGhostEntity(registry, entity) &&
                SampleNameMatchesEntity(registry, entity, transformSample.entityName)) {
                auto& transform = registry.GetComponent<TransformComponent>(entity);
                transform = transformSample.transform;
            }
        }

        for (const auto& physicsSample : sample.entityPhysics) {
            const Entity entity = physicsSample.entity;
            if (registry.HasComponent<PhysicsComponent>(entity) &&
                !IsReplayGhostEntity(registry, entity) &&
                SampleNameMatchesEntity(registry, entity, physicsSample.entityName)) {
                auto& physics = registry.GetComponent<PhysicsComponent>(entity);
                physics.velocity = physicsSample.velocity;
                physics.angularVelocity = physicsSample.angularVelocity;
                physics.orientation = physicsSample.orientation;
            }
        }

        std::unordered_set<std::string> replayPresentNames;
        replayPresentNames.reserve(sample.entityRender.size());
        for (const auto& renderSample : sample.entityRender) {
            if (!renderSample.entityName.empty()) {
                replayPresentNames.insert(renderSample.entityName);
            }
        }

        for (Entity e = 0; e < entityCount; ++e) {
            if (!IsReplayDespawnerCandidate(registry, e)) {
                continue;
            }

            bool isPresentInSample = false;
            if (registry.HasComponent<NameComponent>(e)) {
                const auto& liveName = registry.GetComponent<NameComponent>(e).name;
                isPresentInSample = replayPresentNames.find(liveName) != replayPresentNames.end();
            }

            auto& render = registry.GetComponent<RenderComponent>(e);
            if (isPresentInSample) {
                render.visible = true;
                hiddenReplayEntities.erase(e);
            }
            else {
                render.visible = false;
                hiddenReplayEntities.insert(e);
            }
        }

        std::unordered_set<Entity> usedGhosts;
        usedGhosts.reserve(sample.entityRender.size());

        for (const auto& renderSample : sample.entityRender) {
            const Entity entity = renderSample.entity;
            if (registry.HasComponent<TransformComponent>(entity) &&
                !IsReplayGhostEntity(registry, entity) &&
                SampleNameMatchesEntity(registry, entity, renderSample.entityName)) {
                continue;
            }

            const Entity ghostEntity = EnsureReplayGhostEntity(scene, renderSample);
            if (ghostEntity == MAX_ENTITIES || !registry.HasComponent<TransformComponent>(ghostEntity)) {
                continue;
            }

            auto& ghostTransform = registry.GetComponent<TransformComponent>(ghostEntity);
            ghostTransform = renderSample.transform;

            if (registry.HasComponent<RenderComponent>(ghostEntity)) {
                auto& ghostRender = registry.GetComponent<RenderComponent>(ghostEntity);
                ghostRender.visible = true;
                ghostRender.useDebugOverlay = highlightSpawnedObjects;
                if (highlightSpawnedObjects) {
                    ghostRender.debugOverlayColor = glm::vec4(0.4f, 1.0f, 0.5f, 0.6f);
                }
            }

            usedGhosts.insert(ghostEntity);
        }

        for (auto it = replayGhostEntities.begin(); it != replayGhostEntities.end(); ) {
            const Entity ghostEntity = it->second;
            const bool isValidGhost = (ghostEntity != MAX_ENTITIES) &&
                IsReplayGhostEntity(registry, ghostEntity) &&
                registry.HasComponent<TransformComponent>(ghostEntity);

            if (!isValidGhost || usedGhosts.find(ghostEntity) == usedGhosts.end()) {
                if (isValidGhost) {
                    scene.DeleteEntity(ghostEntity);
                }
                it = replayGhostEntities.erase(it);
            }
            else {
                ++it;
            }
        }

        Entity cameraModelEntity = scene.GetEntityByName(kReplayCameraModelName);

        if (!sample.hasRecordedCamera) {
            if (cameraModelEntity != MAX_ENTITIES && registry.HasComponent<RenderComponent>(cameraModelEntity)) {
                registry.GetComponent<RenderComponent>(cameraModelEntity).visible = false;
            }
            return;
        }

        if (viewRecordedCamera) {
            if (activeCameraEntity != MAX_ENTITIES && registry.HasComponent<TransformComponent>(activeCameraEntity)) {
                auto& camTransform = registry.GetComponent<TransformComponent>(activeCameraEntity);
                camTransform.position = sample.cameraPosition;
                camTransform.rotation = sample.cameraRotation;
                camTransform.UpdateMatrix();
            }

            if (cameraModelEntity != MAX_ENTITIES && registry.HasComponent<RenderComponent>(cameraModelEntity)) {
                registry.GetComponent<RenderComponent>(cameraModelEntity).visible = false;
            }
            return;
        }

        if (cameraModelEntity == MAX_ENTITIES) {
            scene.AddCube(kReplayCameraModelName, sample.cameraPosition, glm::vec3(0.2f), "");
            cameraModelEntity = scene.GetEntityByName(kReplayCameraModelName);
            if (cameraModelEntity != MAX_ENTITIES && registry.HasComponent<RenderComponent>(cameraModelEntity)) {
                auto& render = registry.GetComponent<RenderComponent>(cameraModelEntity);
                render.useDebugOverlay = true;
                render.debugOverlayColor = glm::vec4(1.0f, 0.8f, 0.2f, 1.0f);
            }
        }

        if (cameraModelEntity != MAX_ENTITIES && registry.HasComponent<TransformComponent>(cameraModelEntity)) {
            auto& camModelTransform = registry.GetComponent<TransformComponent>(cameraModelEntity);
            camModelTransform.position = sample.cameraPosition;
            camModelTransform.rotation = sample.cameraRotation;
            camModelTransform.UpdateMatrix();

            if (registry.HasComponent<RenderComponent>(cameraModelEntity)) {
                registry.GetComponent<RenderComponent>(cameraModelEntity).visible = true;
            }
        }
    }

    static void HideReplayCameraModel(Scene& scene) {
        auto& registry = scene.GetRegistry();
        const Entity cameraModelEntity = scene.GetEntityByName(kReplayCameraModelName);
        if (cameraModelEntity != MAX_ENTITIES && registry.HasComponent<RenderComponent>(cameraModelEntity)) {
            registry.GetComponent<RenderComponent>(cameraModelEntity).visible = false;
        }

        CleanupReplayGhosts(scene);
    }

    void Update(Scene& scene, float deltaTime) override {
        auto& registry = scene.GetRegistry();
        const Entity entityCount = registry.GetEntityCount();

        for (Entity e = 0; e < entityCount; ++e) {
            if (!registry.HasComponent<TransformComponent>(e) ||
                !registry.HasComponent<PathAnimationComponent>(e)) {
                continue;
            }

            auto& transform = registry.GetComponent<TransformComponent>(e);
            auto& path = registry.GetComponent<PathAnimationComponent>(e);

            if (path.segments.empty()) {
                path.currentSegmentIndex = 0;
                path.segmentTime = 0.0f;
                continue;
            }

            if (!path.initialized) {
                InitializePathAnimation(path);
            }

            if (path.segments.empty()) {
                continue;
            }

            path.currentSegmentIndex = std::clamp(path.currentSegmentIndex, 0, static_cast<int>(path.segments.size()) - 1);
            if (path.playMode != PathAnimationPlayMode::Bounce) {
                path.direction = path.reversePath ? -1 : 1;
            }
            else if (path.direction != -1 && path.direction != 1) {
                path.direction = path.reversePath ? -1 : 1;
            }

            if (path.isPlaying) {
                const float speed = std::max(0.0f, globalPlaybackSpeed) * std::max(0.0f, path.playbackSpeed);
                AdvancePath(path, std::max(0.0f, deltaTime) * speed);
            }

            if (path.segments.empty()) {
                continue;
            }

            path.currentSegmentIndex = std::clamp(path.currentSegmentIndex, 0, static_cast<int>(path.segments.size()) - 1);
            const PathSegment& segment = path.segments[path.currentSegmentIndex];
            const float duration = std::max(segment.duration, kMinDuration);
            const float localTime = glm::clamp(path.segmentTime, 0.0f, duration);
            const float normalized = localTime / duration;
            const float t = (path.direction >= 0) ? normalized : (1.0f - normalized);

            transform.position = EvaluateSegmentPosition(segment, t);

            if (path.isPlaying && path.applyAnimationVelocity) {
                transform.position += path.animationVelocity * std::max(0.0f, deltaTime);
            }

            if (path.isPlaying && path.collideWithFixedObjects && registry.HasComponent<ColliderComponent>(e)) {
                ResolveAnimatedCollisions(registry, e, transform.position, path.animationVelocity);
            }

            if (path.rotateAlongPath) {
                glm::vec3 tangent = EvaluateSegmentTangent(segment, t);
                if (path.direction < 0) {
                    tangent = -tangent;
                }
                if (path.applyAnimationVelocity) {
                    tangent += path.animationVelocity;
                }

                if (glm::length(tangent) > 0.0001f) {
                    const glm::vec3 direction = glm::normalize(tangent);
                    const float yaw = std::atan2(direction.x, direction.z);
                    const float pitch = -std::asin(glm::clamp(direction.y, -1.0f, 1.0f));
                    transform.rotation = glm::degrees(glm::vec3(pitch, yaw, 0.0f));
                }
            }

            transform.UpdateMatrix();
        }
    }

private:
    struct EntityTransformSample {
        Entity entity = MAX_ENTITIES;
        std::string entityName;
        TransformComponent transform;
    };

    struct EntityPhysicsSample {
        Entity entity = MAX_ENTITIES;
        std::string entityName;
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 angularVelocity = glm::vec3(0.0f);
        glm::mat3 orientation = glm::mat3(1.0f);
    };

    struct EntityRenderSample {
        Entity entity = MAX_ENTITIES;
        std::string entityName;
        TransformComponent transform;
        std::string geometryName = "Sphere";
        std::string texturePath;
    };

    struct LookaheadSample {
        float timeSeconds = 0.0f;
        std::vector<EntityTransformSample> entityTransforms;
        std::vector<EntityPhysicsSample> entityPhysics;
        std::vector<EntityRenderSample> entityRender;
        glm::vec3 cameraPosition = glm::vec3(0.0f);
        glm::vec3 cameraRotation = glm::vec3(0.0f);
        bool hasRecordedCamera = false;
    };

    inline static std::vector<LookaheadSample> lookaheadSamples;
    inline static float lookaheadDurationSeconds = 0.0f;
    inline static bool isRealtimeRecording = false;
    inline static float realtimeRecordingElapsedSeconds = 0.0f;
    static constexpr const char* kReplayCameraModelName = "__ReplayCameraModel";
    static constexpr const char* kReplayGhostPrefix = "__ReplayGhost_";
    inline static std::unordered_map<std::string, Entity> replayGhostEntities;
    inline static std::unordered_set<Entity> hiddenReplayEntities;

    static constexpr float kMinDuration = 0.0001f;

    static bool SampleNameMatchesEntity(Registry& registry, Entity entity, const std::string& sampleName) {
        if (sampleName.empty()) {
            return true;
        }
        if (!registry.HasComponent<NameComponent>(entity)) {
            return false;
        }
        return registry.GetComponent<NameComponent>(entity).name == sampleName;
    }

    static bool IsReplayDespawnerCandidate(Registry& registry, Entity entity) {
        if (entity == MAX_ENTITIES ||
            !registry.HasComponent<RenderComponent>(entity) ||
            !registry.HasComponent<PhysicsComponent>(entity) ||
            IsReplayGhostEntity(registry, entity)) {
            return false;
        }

        if (registry.HasComponent<SpawnedFromSpawnerComponent>(entity)) {
            return true;
        }

        if (registry.HasComponent<NameComponent>(entity)) {
            const auto& name = registry.GetComponent<NameComponent>(entity).name;
            return name.rfind("DynamicBall_", 0) == 0;
        }

        return false;
    }

    static std::string BuildReplayGhostKey(const EntityRenderSample& renderSample) {
        if (!renderSample.entityName.empty()) {
            return renderSample.entityName;
        }
        return std::string("__id_") + std::to_string(renderSample.entity);
    }

    static std::string BuildReplayGhostName(const EntityRenderSample& renderSample) {
        const std::string key = BuildReplayGhostKey(renderSample);
        const size_t keyHash = std::hash<std::string>{}(key);
        return std::string(kReplayGhostPrefix) + std::to_string(keyHash);
    }

    static glm::vec3 EvaluateSegmentPosition(const PathSegment& segment, float t) {
        const float clampedT = AnimationMath::Clamp01(t);
        if (segment.curveType == PathCurveType::BezierQuadratic) {
            return AnimationMath::QuadraticBezier(segment.startPoint, segment.controlPoint, segment.endPoint, clampedT);
        }
        return AnimationMath::LerpVec3(segment.startPoint, segment.endPoint, clampedT);
    }

    static glm::vec3 EvaluateSegmentTangent(const PathSegment& segment, float t) {
        const float clampedT = AnimationMath::Clamp01(t);
        if (segment.curveType == PathCurveType::BezierQuadratic) {
            const glm::vec3 a = segment.controlPoint - segment.startPoint;
            const glm::vec3 b = segment.endPoint - segment.controlPoint;
            return (2.0f * (1.0f - clampedT) * a) + (2.0f * clampedT * b);
        }
        return segment.endPoint - segment.startPoint;
    }

    static LookaheadSample CaptureSceneSample(Scene& scene, float timeSeconds) {
        LookaheadSample sample;
        sample.timeSeconds = std::max(0.0f, timeSeconds);

        auto& registry = scene.GetRegistry();
        const Entity entityCount = registry.GetEntityCount();
        sample.entityTransforms.reserve(entityCount);
        sample.entityPhysics.reserve(entityCount);
        sample.entityRender.reserve(entityCount);

        Entity activeCameraEntity = MAX_ENTITIES;

        for (Entity e = 0; e < entityCount; ++e) {
            if (IsReplayArtifactEntity(registry, e)) {
                continue;
            }

            if (IsNonReplayVisualHelperEntity(registry, e)) {
                continue;
            }

            std::string entityName;
            if (registry.HasComponent<NameComponent>(e)) {
                entityName = registry.GetComponent<NameComponent>(e).name;
            }

            if (registry.HasComponent<TransformComponent>(e)) {
                const auto& transform = registry.GetComponent<TransformComponent>(e);
                sample.entityTransforms.push_back({ e, entityName, transform });
            }

            if (registry.HasComponent<PhysicsComponent>(e)) {
                const auto& physics = registry.GetComponent<PhysicsComponent>(e);
                sample.entityPhysics.push_back({ e, entityName, physics.velocity, physics.angularVelocity, physics.orientation });
            }

            if (registry.HasComponent<TransformComponent>(e) && registry.HasComponent<RenderComponent>(e)) {
                const auto& render = registry.GetComponent<RenderComponent>(e);
                const auto& transform = registry.GetComponent<TransformComponent>(e);
                std::string entityName;
                if (registry.HasComponent<NameComponent>(e)) {
                    entityName = registry.GetComponent<NameComponent>(e).name;
                }
                sample.entityRender.push_back({ e, entityName, transform, render.geometryName, render.texturePath });
            }

            if (activeCameraEntity == MAX_ENTITIES && registry.HasComponent<CameraComponent>(e) &&
                registry.GetComponent<CameraComponent>(e).isActive) {
                activeCameraEntity = e;
            }
        }

        if (activeCameraEntity != MAX_ENTITIES && registry.HasComponent<TransformComponent>(activeCameraEntity)) {
            const auto& cameraTransform = registry.GetComponent<TransformComponent>(activeCameraEntity);
            sample.hasRecordedCamera = true;
            sample.cameraPosition = cameraTransform.position;
            sample.cameraRotation = cameraTransform.rotation;
        }

        return sample;
    }

    static Entity EnsureReplayGhostEntity(Scene& scene, const EntityRenderSample& renderSample) {
        auto& registry = scene.GetRegistry();
        const std::string ghostKey = BuildReplayGhostKey(renderSample);

        auto it = replayGhostEntities.find(ghostKey);
        if (it != replayGhostEntities.end()) {
            const Entity existingGhost = it->second;
            if (existingGhost != MAX_ENTITIES &&
                registry.HasComponent<TransformComponent>(existingGhost) &&
                IsReplayGhostEntity(registry, existingGhost)) {
                return existingGhost;
            }

            DebugLog("Invalid stale ghost mapping for source entity=" + std::to_string(renderSample.entity) +
                " mappedGhost=" + std::to_string(existingGhost));
        }

        const std::string ghostName = BuildReplayGhostName(renderSample);
        Entity ghostEntity = scene.GetEntityByName(ghostName);

        if (ghostEntity == MAX_ENTITIES) {
            std::string geometryNameLower = renderSample.geometryName;
            std::transform(geometryNameLower.begin(), geometryNameLower.end(), geometryNameLower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
                });

            if (geometryNameLower == "cube") {
                scene.AddCube(ghostName, renderSample.transform.position, renderSample.transform.scale, renderSample.texturePath);
            }
            else if (geometryNameLower.find(".obj") != std::string::npos || geometryNameLower.find(".sjg") != std::string::npos) {
                scene.AddModel(
                    ghostName,
                    renderSample.transform.position,
                    renderSample.transform.rotation,
                    renderSample.transform.scale,
                    renderSample.geometryName,
                    renderSample.texturePath,
                    false);
            }
            else if (geometryNameLower == "grid") {
                scene.AddGrid(ghostName, 2, 2, 1.0f, renderSample.transform.position, renderSample.texturePath);
            }
            else if (geometryNameLower == "sphere") {
                const float radius = std::max(0.1f, std::max({ renderSample.transform.scale.x, renderSample.transform.scale.y, renderSample.transform.scale.z }) * 0.5f);
                scene.AddSphere(ghostName, 16, 32, radius, renderSample.transform.position, renderSample.texturePath);
            }
            else {
                const float radius = std::max(0.1f, std::max({ renderSample.transform.scale.x, renderSample.transform.scale.y, renderSample.transform.scale.z }) * 0.5f);
                DebugLog("Ghost fallback sphere for source entity=" + std::to_string(renderSample.entity) +
                    " geometryName='" + renderSample.geometryName + "'");
                scene.AddSphere(ghostName, 16, 32, radius, renderSample.transform.position, renderSample.texturePath);
            }

            ghostEntity = scene.GetEntityByName(ghostName);
        }

        if (ghostEntity != MAX_ENTITIES && registry.HasComponent<RenderComponent>(ghostEntity)) {
            auto& render = registry.GetComponent<RenderComponent>(ghostEntity);
            render.useDebugOverlay = false;
            render.debugOverlayColor = glm::vec4(0.4f, 1.0f, 0.5f, 0.6f);
        }

        replayGhostEntities[ghostKey] = ghostEntity;
        return ghostEntity;
    }

    static void CleanupReplayGhosts(Scene& scene) {
        auto& registry = scene.GetRegistry();
        std::unordered_set<Entity> ghostsToDeleteSet;
        std::vector<Entity> ghostsToDelete;
        ghostsToDelete.reserve(replayGhostEntities.size());

        for (const auto& pair : replayGhostEntities) {
            const Entity ghostEntity = pair.second;
            if (ghostEntity == MAX_ENTITIES || !registry.HasComponent<NameComponent>(ghostEntity)) {
                continue;
            }

            const std::string& name = registry.GetComponent<NameComponent>(ghostEntity).name;
            if (name.rfind(kReplayGhostPrefix, 0) == 0) {
                if (ghostsToDeleteSet.insert(ghostEntity).second) {
                    ghostsToDelete.push_back(ghostEntity);
                }
            }
        }

        const Entity entityCount = registry.GetEntityCount();
        for (Entity e = 0; e < entityCount; ++e) {
            if (!registry.HasComponent<NameComponent>(e)) {
                continue;
            }

            const std::string& name = registry.GetComponent<NameComponent>(e).name;
            if (name.rfind(kReplayGhostPrefix, 0) == 0) {
                if (ghostsToDeleteSet.insert(e).second) {
                    ghostsToDelete.push_back(e);
                }
            }
        }

        for (Entity ghost : ghostsToDelete) {
            scene.DeleteEntity(ghost);
        }

        if (!ghostsToDelete.empty()) {
            DebugLog("CleanupReplayGhosts deleted=" + std::to_string(ghostsToDelete.size()));
        }

        replayGhostEntities.clear();
    }

    static bool IsReplayGhostEntity(Registry& registry, Entity entity) {
        if (!registry.HasComponent<NameComponent>(entity)) {
            return false;
        }
        const std::string& name = registry.GetComponent<NameComponent>(entity).name;
        return name.rfind(kReplayGhostPrefix, 0) == 0;
    }

    static bool IsReplayArtifactEntity(Registry& registry, Entity entity) {
        if (!registry.HasComponent<NameComponent>(entity)) {
            return false;
        }

        const std::string& name = registry.GetComponent<NameComponent>(entity).name;
        return (name == kReplayCameraModelName) || (name.rfind(kReplayGhostPrefix, 0) == 0);
    }

    static bool IsNonReplayVisualHelperEntity(Registry& registry, Entity entity) {
        if (!registry.HasComponent<RenderComponent>(entity)) {
            return false;
        }

        const auto& render = registry.GetComponent<RenderComponent>(entity);
        return (render.geometryName == "path_visual") || (render.geometryName == "spring_visual");
    }

    static void DebugLog(const std::string& message) {
        if (!replayDebugLogging) {
            return;
        }
        std::cout << "[ReplayDebug] " << message << std::endl;
    }

    static void ResolveAnimatedCollisions(Registry& registry, Entity movingEntity, glm::vec3& movingPos, glm::vec3 movingVelocity) {
        auto& movingCollider = registry.GetComponent<ColliderComponent>(movingEntity);
        if (movingCollider.type != 0) {
            return;
        }

        const float movingRadius = std::max(movingCollider.radius, 0.001f);
        const Entity entityCount = registry.GetEntityCount();

        for (Entity other = 0; other < entityCount; ++other) {
            if (other == movingEntity) {
                continue;
            }
            if (!registry.HasComponent<TransformComponent>(other) ||
                !registry.HasComponent<ColliderComponent>(other) ||
                !registry.HasComponent<PhysicsComponent>(other)) {
                continue;
            }
            if (registry.HasComponent<PathAnimationComponent>(other)) {
                continue;
            }

            const auto& otherPhysics = registry.GetComponent<PhysicsComponent>(other);
            if (!otherPhysics.isStatic) {
                continue;
            }

            const auto& otherTransform = registry.GetComponent<TransformComponent>(other);
            const auto& otherCollider = registry.GetComponent<ColliderComponent>(other);

            if (otherCollider.type == 0) {
                const float otherRadius = std::max(otherCollider.radius, 0.001f);
                const glm::vec3 delta = movingPos - otherTransform.position;
                float dist = glm::length(delta);
                const float minDist = movingRadius + otherRadius;

                if (dist < minDist) {
                    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    if (dist > 0.0001f) {
                        normal = delta / dist;
                    }
                    else {
                        dist = 0.0f;
                    }

                    movingPos += normal * (minDist - dist);
                    const float vn = glm::dot(movingVelocity, normal);
                    if (vn < 0.0f) {
                        movingVelocity -= normal * vn;
                    }
                }
            }
            else if (otherCollider.type == 1) {
                glm::vec3 normal = otherCollider.normal;
                if (glm::length(normal) <= 0.0001f) {
                    normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }
                normal = glm::normalize(normal);

                const float signedDist = glm::dot(movingPos - otherTransform.position, normal);
                const float overlap = movingRadius - signedDist;
                if (overlap > 0.0f) {
                    movingPos += normal * overlap;
                    const float vn = glm::dot(movingVelocity, normal);
                    if (vn < 0.0f) {
                        movingVelocity -= normal * vn;
                    }
                }
            }
        }
    }

    static float ComputeSegmentLength(const PathSegment& segment) {
        if (segment.curveType == PathCurveType::BezierQuadratic) {
            return AnimationMath::ApproximateQuadraticBezierLength(segment.startPoint, segment.controlPoint, segment.endPoint, 24);
        }
        return glm::length(segment.endPoint - segment.startPoint);
    }

    static void InitializePathAnimation(PathAnimationComponent& path) {
        if (path.direction != -1 && path.direction != 1) {
            path.direction = path.reversePath ? -1 : 1;
        }

        if (path.segments.empty()) {
            path.currentSegmentIndex = 0;
            path.segmentTime = 0.0f;
            path.initialized = true;
            return;
        }

        path.currentSegmentIndex = std::clamp(path.currentSegmentIndex, 0, static_cast<int>(path.segments.size()) - 1);

        float totalLength = 0.0f;
        for (auto& segment : path.segments) {
            segment.cachedLength = ComputeSegmentLength(segment);
            if (!std::isfinite(segment.cachedLength) || segment.cachedLength < 0.0f) {
                segment.cachedLength = 0.0f;
            }
            totalLength += segment.cachedLength;
        }

        if (path.timingMode == PathAnimationTimingMode::OverallTime) {
            const float safeOverall = std::max(path.overallDuration, kMinDuration * static_cast<float>(path.segments.size()));
            if (totalLength <= kMinDuration) {
                const float perSegment = safeOverall / static_cast<float>(path.segments.size());
                for (auto& segment : path.segments) {
                    segment.duration = std::max(perSegment, kMinDuration);
                }
            }
            else {
                for (auto& segment : path.segments) {
                    const float ratio = segment.cachedLength / totalLength;
                    segment.duration = std::max(safeOverall * ratio, kMinDuration);
                }
            }
        }
        else {
            for (auto& segment : path.segments) {
                segment.duration = std::max(segment.duration, kMinDuration);
            }
        }

        const float currentDuration = path.segments[path.currentSegmentIndex].duration;
        path.segmentTime = glm::clamp(path.segmentTime, 0.0f, currentDuration);
        path.initialized = true;
    }

    static void AdvancePath(PathAnimationComponent& path, float deltaTime) {
        if (deltaTime <= 0.0f || path.segments.empty()) {
            return;
        }

        float remainingTime = deltaTime;

        while (remainingTime > 0.0f && path.isPlaying && !path.segments.empty()) {
            const int segmentCount = static_cast<int>(path.segments.size());
            path.currentSegmentIndex = std::clamp(path.currentSegmentIndex, 0, segmentCount - 1);

            auto& segment = path.segments[path.currentSegmentIndex];
            const float duration = std::max(segment.duration, kMinDuration);
            const float timeToBoundary = std::max(0.0f, duration - path.segmentTime);

            if (remainingTime < timeToBoundary) {
                path.segmentTime += remainingTime;
                remainingTime = 0.0f;
                break;
            }

            path.segmentTime = duration;
            remainingTime -= timeToBoundary;

            const int lastSegmentIndex = segmentCount - 1;

            if (path.direction > 0) {
                if (path.currentSegmentIndex < lastSegmentIndex) {
                    path.currentSegmentIndex += 1;
                    path.segmentTime = 0.0f;
                    continue;
                }

                if (path.playMode == PathAnimationPlayMode::Loop) {
                    path.currentSegmentIndex = 0;
                    path.segmentTime = 0.0f;
                    continue;
                }

                if (path.playMode == PathAnimationPlayMode::Bounce) {
                    path.direction = -1;
                    path.segmentTime = 0.0f;
                    continue;
                }

                path.isPlaying = false;
                path.currentSegmentIndex = lastSegmentIndex;
                path.segmentTime = duration;
                break;
            }
            else {
                if (path.currentSegmentIndex > 0) {
                    path.currentSegmentIndex -= 1;
                    path.segmentTime = 0.0f;
                    continue;
                }

                if (path.playMode == PathAnimationPlayMode::Loop) {
                    path.currentSegmentIndex = lastSegmentIndex;
                    path.segmentTime = 0.0f;
                    continue;
                }

                if (path.playMode == PathAnimationPlayMode::Bounce) {
                    path.direction = 1;
                    path.segmentTime = 0.0f;
                    continue;
                }

                path.isPlaying = false;
                path.currentSegmentIndex = 0;
                path.segmentTime = duration;
                break;
            }
        }
    }
};
