#pragma once

#include "ISystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include "OrbitSystem.h"
#include "PhysicsSystem.h"
#include "../util/AnimationMath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

class AnimationSystem : public ISystem {
public:
    inline static float globalPlaybackSpeed = 1.0f;

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

    static void RecordRealtimeFrame(Scene& scene, float deltaTime) {
        if (!isRealtimeRecording) {
            return;
        }

        realtimeRecordingElapsedSeconds += std::max(0.0f, deltaTime);
        lookaheadDurationSeconds = realtimeRecordingElapsedSeconds;
        lookaheadSamples.push_back(CaptureSceneSample(scene, realtimeRecordingElapsedSeconds));
    }

    static void GenerateLookahead(Scene& scene, float durationSeconds, float stepSeconds = (1.0f / 30.0f)) {
        isRealtimeRecording = false;
        realtimeRecordingElapsedSeconds = 0.0f;
        lookaheadSamples.clear();

        const float safeDuration = std::max(0.0f, durationSeconds);
        const float safeStep = std::max(stepSeconds, 0.001f);
        lookaheadDurationSeconds = safeDuration;

        auto& registry = scene.GetRegistry();
        const Entity entityCount = registry.GetEntityCount();

        struct InitialState {
            Entity entity = MAX_ENTITIES;
            bool hasTransform = false;
            TransformComponent transform;
            bool hasPhysics = false;
            PhysicsComponent physics;
            bool hasPathAnimation = false;
            PathAnimationComponent path;
            bool hasOrbit = false;
            OrbitComponent orbit;
        };

        std::vector<InitialState> initialStates;
        initialStates.reserve(entityCount);

        for (Entity e = 0; e < entityCount; ++e) {
            InitialState state;
            state.entity = e;

            if (registry.HasComponent<TransformComponent>(e)) {
                state.hasTransform = true;
                state.transform = registry.GetComponent<TransformComponent>(e);
            }

            if (registry.HasComponent<PhysicsComponent>(e)) {
                state.hasPhysics = true;
                state.physics = registry.GetComponent<PhysicsComponent>(e);
            }

            if (registry.HasComponent<PathAnimationComponent>(e)) {
                state.hasPathAnimation = true;
                state.path = registry.GetComponent<PathAnimationComponent>(e);
            }

            if (registry.HasComponent<OrbitComponent>(e)) {
                state.hasOrbit = true;
                state.orbit = registry.GetComponent<OrbitComponent>(e);
            }

            initialStates.push_back(state);
        }

        const int sampleCount = static_cast<int>(std::ceil(safeDuration / safeStep)) + 1;
        lookaheadSamples.reserve(static_cast<size_t>(std::max(sampleCount, 1)));

        lookaheadSamples.push_back(CaptureSceneSample(scene, 0.0f));

        OrbitSystem orbitSystem;
        AnimationSystem animationSystem;
        PhysicsSystem physicsSystem;

        for (int i = 1; i < sampleCount; ++i) {
            orbitSystem.Update(scene, safeStep);
            animationSystem.Update(scene, safeStep);
            physicsSystem.Update(scene, safeStep);

            const float t = std::min(static_cast<float>(i) * safeStep, safeDuration);
            lookaheadSamples.push_back(CaptureSceneSample(scene, t));
        }

        for (const auto& state : initialStates) {
            const Entity e = state.entity;
            if (e >= registry.GetEntityCount()) {
                continue;
            }

            if (state.hasTransform && registry.HasComponent<TransformComponent>(e)) {
                registry.GetComponent<TransformComponent>(e) = state.transform;
            }

            if (state.hasPhysics && registry.HasComponent<PhysicsComponent>(e)) {
                registry.GetComponent<PhysicsComponent>(e) = state.physics;
            }

            if (state.hasPathAnimation && registry.HasComponent<PathAnimationComponent>(e)) {
                registry.GetComponent<PathAnimationComponent>(e) = state.path;
            }

            if (state.hasOrbit && registry.HasComponent<OrbitComponent>(e)) {
                registry.GetComponent<OrbitComponent>(e) = state.orbit;
            }
        }
    }

    static bool HasLookahead() {
        return !lookaheadSamples.empty();
    }

    static float GetLookaheadDuration() {
        return lookaheadDurationSeconds;
    }

    static void ScrubLookahead(Scene& scene, float timeSeconds, bool viewRecordedCamera) {
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
            if (registry.HasComponent<TransformComponent>(entity)) {
                auto& transform = registry.GetComponent<TransformComponent>(entity);
                transform = transformSample.transform;
            }
        }

        for (const auto& physicsSample : sample.entityPhysics) {
            const Entity entity = physicsSample.entity;
            if (registry.HasComponent<PhysicsComponent>(entity)) {
                auto& physics = registry.GetComponent<PhysicsComponent>(entity);
                physics.velocity = physicsSample.velocity;
                physics.angularVelocity = physicsSample.angularVelocity;
                physics.orientation = physicsSample.orientation;
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
        TransformComponent transform;
    };

    struct EntityPhysicsSample {
        Entity entity = MAX_ENTITIES;
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 angularVelocity = glm::vec3(0.0f);
        glm::mat3 orientation = glm::mat3(1.0f);
    };

    struct LookaheadSample {
        float timeSeconds = 0.0f;
        std::vector<EntityTransformSample> entityTransforms;
        std::vector<EntityPhysicsSample> entityPhysics;
        glm::vec3 cameraPosition = glm::vec3(0.0f);
        glm::vec3 cameraRotation = glm::vec3(0.0f);
        bool hasRecordedCamera = false;
    };

    inline static std::vector<LookaheadSample> lookaheadSamples;
    inline static float lookaheadDurationSeconds = 0.0f;
    inline static bool isRealtimeRecording = false;
    inline static float realtimeRecordingElapsedSeconds = 0.0f;
    static constexpr const char* kReplayCameraModelName = "__ReplayCameraModel";

    static constexpr float kMinDuration = 0.0001f;

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

        Entity activeCameraEntity = MAX_ENTITIES;

        for (Entity e = 0; e < entityCount; ++e) {
            if (registry.HasComponent<TransformComponent>(e)) {
                const auto& transform = registry.GetComponent<TransformComponent>(e);
                sample.entityTransforms.push_back({ e, transform });
            }

            if (registry.HasComponent<PhysicsComponent>(e)) {
                const auto& physics = registry.GetComponent<PhysicsComponent>(e);
                sample.entityPhysics.push_back({ e, physics.velocity, physics.angularVelocity, physics.orientation });
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
