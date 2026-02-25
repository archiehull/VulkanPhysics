#pragma once
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm/glm.hpp>

constexpr int MAX_LIGHTS = 512;

struct Light
{
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 color;
    alignas(16) glm::vec3 direction; // NEW: Direction the spot is pointing
    float intensity;                 // Offset 44
    int type;                        // Offset 48
    int layerMask;                   // Offset 52
    float cutoffAngle;               // NEW: Precomputed cosine of the angle
    float padding;                   // Offset 60 (Pads the struct to exactly 64 bytes!)
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