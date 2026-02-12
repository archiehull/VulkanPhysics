#pragma once
#include <glm/glm.hpp>

struct PushConstantObject {
    alignas(16) glm::mat4 model;
    alignas(4) int shadingMode; // 0 = Gouraud, 1 = Phong
    alignas(4) int receiveShadows;
    alignas(4) int layerMask;
    alignas(4) float burnFactor; // 0.0 = Normal, 1.0 = Fully Burnt
};