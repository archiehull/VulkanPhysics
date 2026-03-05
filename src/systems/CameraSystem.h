#pragma once
#include "ISystem.h"

class CameraSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;

    static void ToggleNoclip(Scene& scene, Entity cameraEntity = MAX_ENTITIES);
    static void SetNoclip(Scene& scene, bool noclipEnabled, Entity cameraEntity = MAX_ENTITIES);
    static bool IsNoclip(Scene& scene, Entity cameraEntity = MAX_ENTITIES);
};