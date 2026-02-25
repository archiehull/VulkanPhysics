#pragma once

#include "../core/ECS.h"
#include "../core/Config.h"
#include "Camera.h"      
#include <glm/glm.hpp>    
#include "../core/CoreTypes.h"
#include "../core/InputManager.h"
#include <map>
#include <vector>
#include <string>

class Scene;

class CameraController final {
public:
    CameraController(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs);
    ~CameraController() = default;

    void Update(float deltaTime, Scene& scene, const InputManager& input);
    void SwitchCamera(const std::string& name, Scene& scene);
    void SwitchCameraByBind(const std::string& actionBind, Scene& scene);

    Entity GetActiveCameraEntity() const { return activeCameraEntity; }
    std::string GetActiveCameraName() const { return activeCameraName; }
    Entity GetOrbitTarget() const { return OrbitTargetObject; }

    void SetOrbitTarget(Entity target, Scene& scene);

private:
    std::map<std::string, Entity> cameraEntities;
    std::map<std::string, CustomCameraConfig> cameraMeta;
    std::map<std::string, std::string> bindToNameMap; // Maps "Camera1" -> "OutsideCam"

    Entity activeCameraEntity = MAX_ENTITIES;
    std::string activeCameraName = "";
    Entity OrbitTargetObject = MAX_ENTITIES;

    void SetupCameras(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs);

    void UpdateFreeRoam(float deltaTime, Scene& scene, const InputManager& input);
    void UpdateOrbitInput(float deltaTime, Scene& scene, const InputManager& input);
    void ClampCameraPosition(glm::vec3& pos, Scene& scene, const glm::vec3& prevPos) const;
};