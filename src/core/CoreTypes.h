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