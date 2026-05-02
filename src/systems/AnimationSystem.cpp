#include "AnimationSystem.h"
#include "../rendering/Scene.h"
#include "../util/AnimationMath.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace {
bool HasLoopReturnSegment(const PathAnimationComponent& path) {
    return path.connectEndToStart && path.waypoints.size() > 1;
}

size_t GetExpectedSegmentCount(const PathAnimationComponent& path) {
    if (path.waypoints.size() < 2) {
        return 0;
    }
    return HasLoopReturnSegment(path) ? path.waypoints.size() : (path.waypoints.size() - 1);
}

float GetSegmentDurationTotal(const PathAnimationComponent& path) {
    constexpr float kMinDuration = 0.0001f;
    float total = 0.0f;
    for (const auto& segment : path.segments) {
        total += std::max(segment.duration, kMinDuration);
    }
    return total;
}
}

void AnimationSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();
    const Entity entityCount = registry.GetEntityCount();

    auto transformArray = registry.GetComponentArray<TransformComponent>();
    auto pathArray = registry.GetComponentArray<PathAnimationComponent>();
    auto physicsArray = registry.GetComponentArray<PhysicsComponent>();

    for (Entity e = 0; e < entityCount; ++e) {
        if (!transformArray->HasData(e) || !pathArray->HasData(e)) {
            continue;
        }

        auto& transform = transformArray->GetData(e);
        auto& path = pathArray->GetData(e);

        InitializePath(path, transform);
        if (path.waypoints.empty()) {
            path.animationVelocity = glm::vec3(0.0f);
            continue;
        }

        const bool reversePlayback = path.reversePath;
        if (reversePlayback != path.lastReversePath) {
            path.playbackDirection = reversePlayback ? -1 : 1;
            path.lastReversePath = reversePlayback;
        }

        const float authoredEndTime = GetAuthoredEndTime(path);
        const float delta = std::max(0.0f, deltaTime) *
            std::max(0.0f, globalPlaybackSpeed) *
            std::max(0.0f, path.playbackSpeed);

        if (path.isPlaying && delta > 0.0f) {
            AdvancePlayback(path, delta, authoredEndTime);
        }

        if (path.applyConstantRotation) {
            path.rotationSpinTime += std::max(0.0f, deltaTime);
        }

        const EvaluatedPathSegment evaluated = EvaluateSegmentAtTime(path);
        const float easedT = path.applyEasing ? ApplyEasing(path.easing, evaluated.t) : AnimationMath::Clamp01(evaluated.t);

        glm::vec3 localPosition = EvaluateSegmentPosition(evaluated, easedT);
        glm::vec3 localRotation = path.baseRotation;
        if (path.perPointRotation) {
            localRotation = AnimationMath::LerpEulerDegrees(
                evaluated.from.orientation,
                evaluated.to.orientation,
                easedT);
        }
        glm::vec3 tangent = EvaluateSegmentTangent(evaluated, easedT);

        glm::vec3 worldPosition = localPosition;
        if (path.useLocalSpace) {
            worldPosition += path.localOriginPosition;
        }

        const glm::vec3 spinRotation = path.applyConstantRotation
            ? (path.rotationSpinRate * path.rotationSpinTime)
            : glm::vec3(0.0f);

        glm::vec3 worldRotation = localRotation;
        if (path.rotateAlongPath && glm::length(tangent) > 0.0001f) {
            const glm::vec3 direction = glm::normalize(tangent);
            const float yaw = std::atan2(direction.x, direction.z);
            const float pitch = -std::asin(glm::clamp(direction.y, -1.0f, 1.0f));
            worldRotation = glm::degrees(glm::vec3(pitch, yaw, 0.0f)) + path.rotationOffset + spinRotation;
        }
        else if (path.useLocalSpace) {
            worldRotation += path.localOriginRotation;
        }

        if (!path.rotateAlongPath) {
            worldRotation += spinRotation;
        }

        glm::vec3 velocity = glm::vec3(0.0f);
        if (deltaTime > 0.0f) {
            velocity = (worldPosition - transform.position) / deltaTime;
        }

        transform.position = worldPosition;
        transform.rotation = worldRotation;
        transform.UpdateMatrix();

        path.animationVelocity = velocity;
        path.lastEvaluatedPosition = worldPosition;
        path.hasLastEvaluatedPosition = true;

        if (physicsArray->HasData(e)) {
            auto& physics = physicsArray->GetData(e);
            physics.velocity = velocity;
            physics.forceAccumulator = glm::vec3(0.0f);
            physics.torqueAccumulator = glm::vec3(0.0f);

            // Write animated orientation into physics component so collision
            // geometry uses the rotated capsule/cylinder.
            physics.orientation = glm::mat3_cast(glm::quat(glm::radians(transform.rotation)));

            // If this path applies a constant rotation, expose the angular velocity
            // to the physics solver (convert degrees/sec to radians/sec).
            if (path.applyConstantRotation) {
                physics.angularVelocity = glm::radians(path.rotationSpinRate);
            }
            else {
                physics.angularVelocity = glm::vec3(0.0f);
            }

            // Keep animated objects static in terms of linear integration
            // so they follow the authored path, but their rotational state
            // is visible to the collision solver.
            physics.isStatic = true;
            physics.SetMass(0.0f);
        }
    }
}

void AnimationSystem::InitializePath(PathAnimationComponent& path, const TransformComponent& transform) {
    if (path.useLocalSpace && !path.hasLocalOrigin) {
        path.localOriginPosition = transform.position;
        path.localOriginRotation = transform.rotation;
        path.hasLocalOrigin = true;
    }

    if (!path.hasBaseRotation) {
        path.baseRotation = transform.rotation;
        path.hasBaseRotation = true;
    }

    if (path.initialized) {
        return;
    }

    std::sort(path.waypoints.begin(), path.waypoints.end(), [](const PathWaypoint& a, const PathWaypoint& b) {
        return a.timeFromStart < b.timeFromStart;
        });

    EnsureSegmentAlignment(path);

    if (path.timingMode == PathAnimationTimingMode::PerSegment) {
        RebuildWaypointTimesFromSegments(path);
    }
    else if (path.timingMode == PathAnimationTimingMode::OverallTime) {
        RebuildSegmentDurationsFromTotalTime(path);
        RebuildWaypointTimesFromSegments(path);
    }
    else {
        if (!path.waypoints.empty()) {
            path.waypoints.front().timeFromStart = std::max(0.0f, path.waypoints.front().timeFromStart);
            for (size_t i = 1; i < path.waypoints.size(); ++i) {
                path.waypoints[i].timeFromStart = std::max(path.waypoints[i].timeFromStart, path.waypoints[i - 1].timeFromStart);
            }
            for (size_t i = 0; i < path.segments.size() && i + 1 < path.waypoints.size(); ++i) {
                path.segments[i].duration = std::max(
                    kMinDuration,
                    path.waypoints[i + 1].timeFromStart - path.waypoints[i].timeFromStart);
            }
        }
    }

    const float authoredEndTime = GetAuthoredEndTime(path);
    path.totalDuration = std::max(path.totalDuration, std::max(authoredEndTime, GetPlaybackDuration(path)));

    path.currentTime = glm::clamp(path.currentTime, 0.0f, GetPlaybackDuration(path));
    const bool reversePlayback = path.reversePath;
    path.playbackDirection = reversePlayback ? -1 : 1;
    path.lastReversePath = reversePlayback;
    path.initialized = true;
}

float AnimationSystem::GetAuthoredEndTime(const PathAnimationComponent& path) {
    if (path.waypoints.empty()) {
        return 0.0f;
    }
    return std::max(0.0f, path.waypoints.back().timeFromStart);
}

float AnimationSystem::GetLoopReturnDuration(const PathAnimationComponent& path) {
    if (!HasLoopReturnSegment(path) || path.segments.empty()) {
        return 0.0f;
    }
    return std::max(path.segments.back().duration, kMinDuration);
}

float AnimationSystem::GetPlaybackDuration(const PathAnimationComponent& path) {
    const float authoredEndTime = GetAuthoredEndTime(path);
    if (path.playMode == PathAnimationPlayMode::Loop) {
        if (HasLoopReturnSegment(path)) {
            return std::max(GetSegmentDurationTotal(path), kMinDuration);
        }
        return std::max(authoredEndTime, 0.0f);
    }
    return std::max(authoredEndTime, 0.0f);
}

void AnimationSystem::EnsureSegmentAlignment(PathAnimationComponent& path) {
    if (path.waypoints.size() < 2) {
        path.segments.clear();
        return;
    }

    const size_t targetSegmentCount = GetExpectedSegmentCount(path);

    while (path.segments.size() < targetSegmentCount) {
        PathCurveSegment segment;
        const size_t index = path.segments.size();
        const size_t startIndex = index;
        const size_t endIndex = (HasLoopReturnSegment(path) && index + 1 == path.waypoints.size()) ? 0 : index + 1;
        segment.curveType = PathCurveType::Straight;
        segment.controlPoint = (path.waypoints[startIndex].position + path.waypoints[endIndex].position) * 0.5f;
        segment.duration = 1.0f;
        path.segments.push_back(segment);
    }

    if (path.segments.size() > targetSegmentCount) {
        path.segments.resize(targetSegmentCount);
    }
}

void AnimationSystem::RebuildWaypointTimesFromSegments(PathAnimationComponent& path) {
    if (path.waypoints.empty()) {
        return;
    }

    path.waypoints.front().timeFromStart = 0.0f;
    float accumulated = 0.0f;
    const size_t segmentCount = std::min(path.segments.size(), path.waypoints.size() > 0 ? path.waypoints.size() - 1 : size_t(0));
    for (size_t i = 0; i < segmentCount; ++i) {
        accumulated += std::max(kMinDuration, path.segments[i].duration);
        path.waypoints[i + 1].timeFromStart = accumulated;
    }
}

void AnimationSystem::RebuildSegmentDurationsFromTotalTime(PathAnimationComponent& path) {
    if (path.waypoints.size() < 2) {
        return;
    }

    float totalLength = 0.0f;
    const size_t segmentCount = std::min(path.segments.size(), GetExpectedSegmentCount(path));
    for (size_t i = 0; i < segmentCount; ++i) {
        const glm::vec3 start = path.waypoints[i].position;
        const size_t endIndex = (HasLoopReturnSegment(path) && i + 1 == path.waypoints.size()) ? 0 : (i + 1);
        const glm::vec3 end = path.waypoints[endIndex].position;
        float length = 0.0f;
        if (path.segments[i].curveType == PathCurveType::BezierQuadratic) {
            length = AnimationMath::ApproximateQuadraticBezierLength(start, path.segments[i].controlPoint, end, 24);
        }
        else {
            length = glm::length(end - start);
        }
        totalLength += std::max(0.0f, length);
    }

    const float safeTotal = std::max(path.totalDuration, kMinDuration * static_cast<float>(path.segments.size()));
    if (totalLength <= kMinDuration) {
        const float perSegment = safeTotal / static_cast<float>(std::max<size_t>(1, segmentCount));
        for (auto& segment : path.segments) {
            segment.duration = std::max(perSegment, kMinDuration);
        }
        return;
    }

    for (size_t i = 0; i < segmentCount; ++i) {
        const glm::vec3 start = path.waypoints[i].position;
        const size_t endIndex = (HasLoopReturnSegment(path) && i + 1 == path.waypoints.size()) ? 0 : (i + 1);
        const glm::vec3 end = path.waypoints[endIndex].position;
        float length = 0.0f;
        if (path.segments[i].curveType == PathCurveType::BezierQuadratic) {
            length = AnimationMath::ApproximateQuadraticBezierLength(start, path.segments[i].controlPoint, end, 24);
        }
        else {
            length = glm::length(end - start);
        }
        const float ratio = std::max(0.0f, length) / totalLength;
        path.segments[i].duration = std::max(safeTotal * ratio, kMinDuration);
    }
}

void AnimationSystem::AdvancePlayback(PathAnimationComponent& path, float deltaTime, float authoredEndTime) {
    if (path.waypoints.empty()) {
        return;
    }

    if (path.playMode == PathAnimationPlayMode::Bounce) {
        const float limit = std::max(authoredEndTime, 0.0f);
        if (limit <= 0.0f) {
            path.currentTime = 0.0f;
            path.isPlaying = false;
            return;
        }

        float remaining = deltaTime;
        while (remaining > 0.0f) {
            if (path.playbackDirection >= 0) {
                const float toEnd = limit - path.currentTime;
                if (remaining <= toEnd) {
                    path.currentTime += remaining;
                    remaining = 0.0f;
                }
                else {
                    path.currentTime = limit;
                    remaining -= toEnd;
                    path.playbackDirection = -1;
                }
            }
            else {
                const float toStart = path.currentTime;
                if (remaining <= toStart) {
                    path.currentTime -= remaining;
                    remaining = 0.0f;
                }
                else {
                    path.currentTime = 0.0f;
                    remaining -= toStart;
                    path.playbackDirection = 1;
                }
            }
        }
        return;
    }

    const float direction = path.reversePath ? -1.0f : 1.0f;
    path.currentTime += deltaTime * direction;

    if (path.playMode == PathAnimationPlayMode::Loop) {
        const float duration = std::max(GetPlaybackDuration(path), kMinDuration);
        path.currentTime = std::fmod(path.currentTime, duration);
        if (path.currentTime < 0.0f) {
            path.currentTime += duration;
        }
    }
    else {
        const float duration = std::max(authoredEndTime, 0.0f);
        if (path.currentTime <= 0.0f) {
            path.currentTime = 0.0f;
            if (path.reversePath) {
                path.isPlaying = false;
            }
        }
        else if (path.currentTime >= duration) {
            path.currentTime = duration;
            if (!path.reversePath) {
                path.isPlaying = false;
            }
        }
    }
}

float AnimationSystem::ApplyEasing(PathAnimationEasing easing, float t) {
    if (easing == PathAnimationEasing::Smoothstep) {
        return AnimationMath::Smoothstep(t);
    }
    return AnimationMath::Clamp01(t);
}

AnimationSystem::EvaluatedPathSegment AnimationSystem::EvaluateSegmentAtTime(const PathAnimationComponent& path) {
    EvaluatedPathSegment result{};
    if (path.waypoints.empty()) {
        return result;
    }

    if (path.waypoints.size() == 1) {
        result.from = path.waypoints.front();
        result.to = path.waypoints.front();
        result.segmentDuration = kMinDuration;
        result.segment = PathCurveSegment{};
        result.t = 0.0f;
        return result;
    }

    const float authoredEndTime = GetAuthoredEndTime(path);
    const float playbackDuration = std::max(GetPlaybackDuration(path), kMinDuration);
    float localTime = path.currentTime;

    if (path.playMode == PathAnimationPlayMode::Loop) {
        localTime = std::fmod(localTime, playbackDuration);
        if (localTime < 0.0f) {
            localTime += playbackDuration;
        }

        if (HasLoopReturnSegment(path) && localTime > authoredEndTime) {
            result.from = path.waypoints.back();
            result.to = path.waypoints.front();
            result.segment = path.segments.back();
            result.segmentDuration = GetLoopReturnDuration(path);
            result.t = glm::clamp((localTime - authoredEndTime) / result.segmentDuration, 0.0f, 1.0f);
            return result;
        }
    }
    else {
        localTime = glm::clamp(localTime, 0.0f, std::max(authoredEndTime, 0.0f));
    }

    for (size_t i = 1; i < path.waypoints.size(); ++i) {
        const auto& from = path.waypoints[i - 1];
        const auto& to = path.waypoints[i];
        if (localTime <= to.timeFromStart || i == path.waypoints.size() - 1) {
            const float duration = std::max(to.timeFromStart - from.timeFromStart, kMinDuration);
            const float localSegmentTime = glm::clamp(localTime - from.timeFromStart, 0.0f, duration);
            result.from = from;
            result.to = to;
            result.segment = path.segments[std::min(i - 1, path.segments.size() - 1)];
            result.segmentDuration = duration;
            result.t = localSegmentTime / duration;
            return result;
        }
    }

    result.from = path.waypoints[path.waypoints.size() - 2];
    result.to = path.waypoints.back();
    result.segment = path.segments.back();
    result.segmentDuration = std::max(result.to.timeFromStart - result.from.timeFromStart, kMinDuration);
    result.t = 1.0f;
    return result;
}

glm::vec3 AnimationSystem::EvaluateSegmentPosition(const EvaluatedPathSegment& evaluated, float t) {
    if (evaluated.segment.curveType == PathCurveType::BezierQuadratic) {
        return AnimationMath::QuadraticBezier(
            evaluated.from.position,
            evaluated.segment.controlPoint,
            evaluated.to.position,
            t);
    }
    return AnimationMath::LerpVec3(evaluated.from.position, evaluated.to.position, t);
}

glm::vec3 AnimationSystem::EvaluateSegmentTangent(const EvaluatedPathSegment& evaluated, float t) {
    if (evaluated.segment.curveType == PathCurveType::BezierQuadratic) {
        const float clampedT = AnimationMath::Clamp01(t);
        const glm::vec3 a = evaluated.segment.controlPoint - evaluated.from.position;
        const glm::vec3 b = evaluated.to.position - evaluated.segment.controlPoint;
        return (2.0f * (1.0f - clampedT) * a) + (2.0f * clampedT * b);
    }
    return evaluated.to.position - evaluated.from.position;
}
