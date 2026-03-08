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
    constexpr int MAX_LAYERS = 8;
    constexpr int ALL = 0xFFFFFFFF;
    constexpr int ALL_USED = (1 << MAX_LAYERS) - 1;

    constexpr int LAYER_A = 1 << 0;
    constexpr int LAYER_B = 1 << 1;

    inline int ActiveLayerCount = 1;
    inline std::string LayerNames[MAX_LAYERS] = {
        "Base World", "Layer B", "Layer C", "Layer D",
        "Layer E", "Layer F", "Layer G", "Layer H"
    };
}

enum class Season {
    SUMMER,
    AUTUMN,
    WINTER,
    SPRING
};
