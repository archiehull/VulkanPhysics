#pragma once
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm/glm.hpp>

constexpr int MAX_LIGHTS = 512;

struct Light
{
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 color;
    float intensity;
    int type;
    int layerMask;
    float padding;
};

struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec3 viewPos;
    alignas(16) glm::mat4 lightSpaceMatrix;
    alignas(16) Light lights[MAX_LIGHTS];
    alignas(4) int numLights;
    alignas(4) float dayNightFactor;
};