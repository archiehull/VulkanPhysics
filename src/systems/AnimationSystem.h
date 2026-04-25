#pragma once

#include "ISystem.h"
#include "../core/Components.h"

class AnimationSystem : public ISystem {
public:
    inline static float globalPlaybackSpeed = 1.0f;

    void Update(Scene& scene, float deltaTime) override;

private:
    static constexpr float kMinDuration = 0.0001f;

    struct EvaluatedPathSegment {
        PathWaypoint from;
        PathWaypoint to;
        PathCurveSegment segment;
        float segmentDuration = kMinDuration;
        float t = 0.0f;
    };

    static void InitializePath(PathAnimationComponent& path, const TransformComponent& transform);
    static float GetAuthoredEndTime(const PathAnimationComponent& path);
    static float GetPlaybackDuration(const PathAnimationComponent& path);
    static float GetLoopReturnDuration(const PathAnimationComponent& path);
    static void RebuildWaypointTimesFromSegments(PathAnimationComponent& path);
    static void RebuildSegmentDurationsFromTotalTime(PathAnimationComponent& path);
    static void EnsureSegmentAlignment(PathAnimationComponent& path);
    static void AdvancePlayback(PathAnimationComponent& path, float deltaTime, float authoredEndTime);
    static float ApplyEasing(PathAnimationEasing easing, float t);
    static EvaluatedPathSegment EvaluateSegmentAtTime(const PathAnimationComponent& path);
    static glm::vec3 EvaluateSegmentPosition(const EvaluatedPathSegment& evaluated, float t);
    static glm::vec3 EvaluateSegmentTangent(const EvaluatedPathSegment& evaluated, float t);
};
