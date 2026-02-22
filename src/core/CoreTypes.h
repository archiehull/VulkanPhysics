#pragma once

#include <glm/glm.hpp>

enum class ObjectState {
    NORMAL,
    HEATING,
    BURNING,
    BURNT,
    REGROWING
};

namespace SceneLayers {
    constexpr int INSIDE = 1 << 0;
    constexpr int OUTSIDE = 1 << 1;
    constexpr int ALL = INSIDE | OUTSIDE;
}

enum class Season {
    SUMMER,
    AUTUMN,
    WINTER,
    SPRING
};

enum class CameraType {
    FREE_ROAM,
    OUTSIDE_ORB,
    CACTI,
    // --- NEW TYPES ---
    CUSTOM_1,
    CUSTOM_2,
    CUSTOM_3,
    CUSTOM_4
};
