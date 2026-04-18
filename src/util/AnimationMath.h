#pragma once

#include <glm/glm.hpp>
#include <algorithm>

namespace AnimationMath {

inline float Clamp01(float t) {
    return std::clamp(t, 0.0f, 1.0f);
}

inline glm::vec3 LerpVec3(const glm::vec3& a, const glm::vec3& b, float t) {
    const float clamped = Clamp01(t);
    return a + ((b - a) * clamped);
}

inline glm::vec3 QuadraticBezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, float t) {
    const float clamped = Clamp01(t);
    const float inv = 1.0f - clamped;
    return (inv * inv * p0) + (2.0f * inv * clamped * p1) + (clamped * clamped * p2);
}

inline float ApproximateQuadraticBezierLength(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, int samples = 24) {
    const int sampleCount = std::max(2, samples);
    float length = 0.0f;
    glm::vec3 prev = QuadraticBezier(p0, p1, p2, 0.0f);

    for (int i = 1; i <= sampleCount; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleCount);
        const glm::vec3 curr = QuadraticBezier(p0, p1, p2, t);
        length += glm::length(curr - prev);
        prev = curr;
    }

    return length;
}

}
