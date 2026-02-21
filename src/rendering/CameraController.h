#pragma once

#include "Camera.h"
#include "../core/Config.h" // Added for CustomCameraConfig
#include "../core/ECS.h"
#include <memory>
#include <map>
#include <vector>

class Scene;
struct SceneObject;

class CameraController final {
public:
    // Updated constructor to take custom camera configs
    CameraController(const std::vector<CustomCameraConfig>& customConfigs);
    ~CameraController() = default;

    // Non-copyable
    CameraController(const CameraController&) = delete;
    CameraController& operator=(const CameraController&) = delete;

    void Update(float deltaTime, Scene& scene);

    inline Camera* GetActiveCamera() const { return activeCamera; }
    CameraType GetActiveCameraType() const { return activeCameraType; }

    void SwitchCamera(CameraType type, Scene& scene);

    // NEW: Getter for the current Orbit target (for F4 Ignition)
    Entity GetOrbitTarget() const { return OrbitTargetObject; }

    // Input handling
    void OnKeyPress(int key, bool pressed);
    inline void OnKeyRelease(int key) { OnKeyPress(key, false); }

private:
    std::map<CameraType, std::unique_ptr<Camera>> cameras;

    // Store type information for custom cameras
    struct CustomCameraInfo {
        std::string name;
        std::string type; // "FreeRoam", "Orbit", "Static"
        glm::vec3 initialTarget;
    };
    std::map<CameraType, CustomCameraInfo> customCameraMeta;

    Camera* activeCamera = nullptr;
    CameraType activeCameraType = CameraType::FREE_ROAM;

    // Key states
    bool keyW = false, keyA = false, keyS = false, keyD = false;
    bool keyI = false, keyJ = false, keyK = false, keyL = false;
    bool keyQ = false, keyE = false;
    bool keyUp = false, keyDown = false, keyLeft = false, keyRight = false;
    bool keyCtrl = false;
    bool keyShift = false;

    // Orbit Camera State
    Entity OrbitTargetObject = MAX_ENTITIES;
    glm::vec3 FixedOrbitCenter = glm::vec3(0.0f); // For custom orbits that don't target an object
    float OrbitRadius = 15.0f;
    float OrbitYaw = 0.0f;
    float OrbitPitch = 20.0f;

    void SetupCameras(const std::vector<CustomCameraConfig>& customConfigs);
    void UpdateFreeRoamCamera(float deltaTime, Scene& scene);
    void UpdateOrbitCamera(float deltaTime, Scene& scene);
    void ClampCameraPosition(glm::vec3& position, Scene& scene, const glm::vec3& previousPosition) const;

};
