#pragma once

#include "ISystem.h"
#include "../core/Components.h"
#include "../rendering/Scene.h"
#include "../util/AnimationMath.h"
#include <algorithm>
#include <cmath>
#include <vector>

class AnimationSystem : public ISystem {
public:
    inline static float globalPlaybackSpeed = 1.0f;

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
