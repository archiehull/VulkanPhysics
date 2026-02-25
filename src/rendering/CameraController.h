#pragma once

#include "../core/ECS.h"
#include "../core/Config.h"
#include "Camera.h"      
#include <glm/glm.hpp>    
#include "../core/CoreTypes.h"
#include "../core/InputManager.h" // Added InputManager
#include <map>
#include <vector>
#include <string>

class Scene;

class CameraController final {
public:
    CameraController(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs);
    ~CameraController() = default;

    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    // Signature updated to take the InputManager
    void Update(float deltaTime, Scene& scene, const InputManager& input);
    void SwitchCamera(CameraType type, Scene& scene);

    Entity GetActiveCameraEntity() const { return activeCameraEntity; }
    CameraType GetActiveCameraType() const { return activeCameraType; }

    // Removed OnKeyPress and OnKeyRelease entirely

    Entity GetOrbitTarget() const { return OrbitTargetObject; }
private:
    std::map<CameraType, Entity> cameraEntities;

    struct CustomCameraInfo {
        std::string name;
        std::string type; // "FreeRoam", "Orbit", "Static"
        glm::vec3 initialTarget;
    };
    std::map<CameraType, CustomCameraInfo> customCameraMeta;

    Entity activeCameraEntity = MAX_ENTITIES;
    CameraType activeCameraType = CameraType::OUTSIDE_ORB;

    Entity OrbitTargetObject = MAX_ENTITIES;

    // Removed all hardcoded key states (keyW, keyA, keyShift, etc.)

    void SetupCameras(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs);

    // Signatures updated to take the InputManager
    void UpdateFreeRoam(float deltaTime, Scene& scene, const InputManager& input);
    void UpdateOrbitInput(float deltaTime, Scene& scene, const InputManager& input);

    void ClampCameraPosition(glm::vec3& pos, Scene& scene, const glm::vec3& prevPos) const;
};