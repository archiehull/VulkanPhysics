#pragma once

#include "../core/ECS.h"
#include "../core/Config.h"
#include "Camera.h"      // - Needed for CameraType enum
#include <glm/glm.hpp>    // Needed for glm::vec3 in CustomCameraInfo
#include "../core/CoreTypes.h"
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

    void Update(float deltaTime, Scene& scene);
    void SwitchCamera(CameraType type, Scene& scene);

    Entity GetActiveCameraEntity() const { return activeCameraEntity; }
    CameraType GetActiveCameraType() const { return activeCameraType; }

    void OnKeyPress(int key, bool pressed);
    inline void OnKeyRelease(int key) { OnKeyPress(key, false); }

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

    // Input states
    bool keyW = false, keyA = false, keyS = false, keyD = false;
    bool keyQ = false, keyE = false;
    bool keyUp = false, keyDown = false, keyLeft = false, keyRight = false;
    bool keyCtrl = false;
    bool keyShift = false;

    void SetupCameras(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs);
    void UpdateFreeRoam(float deltaTime, Scene& scene);
    void UpdateOrbitInput(float deltaTime, Scene& scene);

    // Optional: Collision clamping for free roam
    void ClampCameraPosition(glm::vec3& pos, Scene& scene, const glm::vec3& prevPos) const;
};