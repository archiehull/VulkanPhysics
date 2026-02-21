#include "CameraController.h"
#include "Scene.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include <random>

CameraController::CameraController(const std::vector<CustomCameraConfig>& customConfigs)
    : activeCamera(nullptr)
    , activeCameraType(CameraType::OUTSIDE_ORB)
{
    SetupCameras(customConfigs);

    // Set default orbit parameters for the starting Outside camera
    OrbitRadius = 350.0f;
    OrbitPitch = 20.0f;
    OrbitYaw = 0.0f;
    OrbitTargetObject = MAX_ENTITIES;

    // Default pointer set to Birds Eye to match activeCameraType
    activeCamera = cameras[CameraType::OUTSIDE_ORB].get();
}

void CameraController::SetupCameras(const std::vector<CustomCameraConfig>& customConfigs) {
    // 1. FREE ROAM Camera (F2)
    auto freeRoamCam = std::make_unique<Camera>();
    freeRoamCam->SetPosition(glm::vec3(0.0f, -75.0f, 0.0f));
    freeRoamCam->SetTarget(glm::vec3(0.0f, -75.0f, 10.0f));
    freeRoamCam->SetMoveSpeed(35.0f);
    freeRoamCam->SetRotateSpeed(45.0f);
    cameras[CameraType::FREE_ROAM] = std::move(freeRoamCam);

    // 2. OUTSIDE Camera (F1)
    auto outsideCam = std::make_unique<Camera>();
    outsideCam->SetPosition(glm::vec3(0.0f, 60.0f, 350.0f));
    outsideCam->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));
    cameras[CameraType::OUTSIDE_ORB] = std::move(outsideCam);

    // 3. Orbit Camera (F3)
    auto OrbitCam = std::make_unique<Camera>();
    OrbitCam->SetPosition(glm::vec3(20.0f, 10.0f, 20.0f));
    OrbitCam->SetTarget(glm::vec3(0.0f, 0.0f, 0.0f));
    cameras[CameraType::CACTI] = std::move(OrbitCam);

    // 4. Custom Cameras (F5-F8)
    CameraType customTypes[] = { CameraType::CUSTOM_1, CameraType::CUSTOM_2, CameraType::CUSTOM_3, CameraType::CUSTOM_4 };

    for (size_t i = 0; i < customConfigs.size() && i < 4; ++i) {
        const auto& conf = customConfigs[i];
        CameraType type = customTypes[i];

        auto cam = std::make_unique<Camera>();
        cam->SetPosition(conf.position);
        cam->SetTarget(conf.target);

        // Store metadata
        CustomCameraInfo info;
        info.name = conf.name;
        info.type = conf.type;
        info.initialTarget = conf.target;
        customCameraMeta[type] = info;

        cameras[type] = std::move(cam);
        std::cout << "Loaded Custom Camera [" << i + 1 << "]: " << conf.name << " (" << conf.type << ")" << std::endl;
    }
}

void CameraController::SwitchCamera(CameraType type, Scene& scene) {
    if (cameras.find(type) == cameras.end()) return;

    // Handle Standard Types
    if (type == CameraType::CACTI) {
        std::vector<Entity> cacti;
        Registry& registry = scene.GetRegistry();

        for (Entity e : scene.GetRenderableEntities()) {
            if (registry.HasComponent<RenderComponent>(e)) {
                if (registry.GetComponent<RenderComponent>(e).texturePath.find("cactus") != std::string::npos) {
                    cacti.push_back(e);
                }
            }
        }

        if (!cacti.empty()) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, static_cast<int>(cacti.size()) - 1);
            OrbitTargetObject = cacti[dis(gen)];
            OrbitRadius = 15.0f;
            OrbitYaw = 0.0f;
            OrbitPitch = 20.0f;

            std::string objName = "Unknown";
            if (registry.HasComponent<NameComponent>(OrbitTargetObject)) {
                objName = registry.GetComponent<NameComponent>(OrbitTargetObject).name;
            }
            std::cout << "Orbiting Cactus: " << objName << std::endl;
        }
        else {
            type = CameraType::FREE_ROAM;
        }
    }
    else if (type == CameraType::FREE_ROAM) {
        // Reset Logic if needed
    }
    else if (type == CameraType::OUTSIDE_ORB) {
        OrbitTargetObject = MAX_ENTITIES;
        OrbitRadius = 350.0f;
        OrbitYaw = 0.0f;
        OrbitPitch = 20.0f;
    }

    // Handle Custom Types
    if (customCameraMeta.find(type) != customCameraMeta.end()) {
        const auto& info = customCameraMeta[type];
        std::cout << "Switched to Custom Camera: " << info.name << std::endl;

        if (info.type == "Orbit") {
            OrbitTargetObject = MAX_ENTITIES; // Custom orbit usually orbits a fixed point
            FixedOrbitCenter = info.initialTarget;
            glm::vec3 diff = cameras[type]->GetPosition() - info.initialTarget;
            OrbitRadius = glm::length(diff);
        }
    }

    activeCameraType = type;
    activeCamera = cameras[type].get();
}

void CameraController::Update(float deltaTime, Scene& scene) {
    if (!activeCamera) return;

    // Check if current is custom
    if (customCameraMeta.find(activeCameraType) != customCameraMeta.end()) {
        const auto& info = customCameraMeta[activeCameraType];
        if (info.type == "FreeRoam") {
            UpdateFreeRoamCamera(deltaTime, scene);
        }
        else if (info.type == "Orbit") {
            UpdateOrbitCamera(deltaTime, scene);
        }
        // If "Static", do nothing (no update call)
        return;
    }

    // Standard types
    switch (activeCameraType) {
    case CameraType::FREE_ROAM:
        UpdateFreeRoamCamera(deltaTime, scene);
        break;
    case CameraType::CACTI:
    case CameraType::OUTSIDE_ORB:
        UpdateOrbitCamera(deltaTime, scene);
        break;
    }
}

void CameraController::UpdateOrbitCamera(float deltaTime, Scene& scene) {
    // Determine Target Position
    glm::vec3 targetPos;

    // 1. Cacti Mode: Follow Object
    if (activeCameraType == CameraType::CACTI && OrbitTargetObject != MAX_ENTITIES) {
        Registry& registry = scene.GetRegistry();
        if (registry.HasComponent<TransformComponent>(OrbitTargetObject)) {
            targetPos = glm::vec3(registry.GetComponent<TransformComponent>(OrbitTargetObject).matrix[3]);
            targetPos.y += 3.0f;
        }
    }
    // 2. Custom Orbit Mode: Follow Fixed Point
    else if (customCameraMeta.find(activeCameraType) != customCameraMeta.end()) {
        targetPos = customCameraMeta[activeCameraType].initialTarget;
    }
    // 3. Outside Orb: Follow Center
    else {
        targetPos = glm::vec3(0.0f, 0.0f, 0.0f);
    }

    const float rotateSpeed = 50.0f;
    const float zoomSpeed = 50.0f;

    if (keyA || keyLeft)  OrbitYaw -= rotateSpeed * deltaTime;
    if (keyD || keyRight) OrbitYaw += rotateSpeed * deltaTime;
    if (keyW || keyUp)    OrbitPitch += rotateSpeed * deltaTime;
    if (keyS || keyDown)  OrbitPitch -= rotateSpeed * deltaTime;

    if (keyQ) OrbitRadius -= zoomSpeed * deltaTime;
    if (keyE) OrbitRadius += zoomSpeed * deltaTime;

    // Clamp defaults
    OrbitPitch = std::clamp(OrbitPitch, -89.0f, 89.0f);
    if (OrbitRadius < 1.0f) OrbitRadius = 1.0f;

    const float radYaw = glm::radians(OrbitYaw);
    const float radPitch = glm::radians(OrbitPitch);

    glm::vec3 offset;
    offset.x = OrbitRadius * cos(radPitch) * sin(radYaw);
    offset.y = OrbitRadius * sin(radPitch);
    offset.z = OrbitRadius * cos(radPitch) * cos(radYaw);

    glm::vec3 newPos = targetPos + offset;

    activeCamera->SetPosition(newPos);
    activeCamera->SetTarget(targetPos);
}

void CameraController::UpdateFreeRoamCamera(float deltaTime, Scene& scene) {
    const bool groupA_forward = keyW;
    const bool groupA_backward = keyS;
    const bool groupA_left = keyA;
    const bool groupA_right = keyD;
    const bool groupB_forward = keyUp;
    const bool groupB_backward = keyDown;
    const bool groupB_left = keyLeft;
    const bool groupB_right = keyRight;

    const bool moveForward = keyCtrl ? groupB_forward : groupA_forward;
    const bool moveBackward = keyCtrl ? groupB_backward : groupA_backward;
    const bool moveLeft = keyCtrl ? groupB_left : groupA_left;
    const bool moveRight = keyCtrl ? groupB_right : groupA_right;
    const bool moveUp = keyE;
    const bool moveDown = keyQ;

    const bool rotatePitchUp = keyCtrl ? groupA_forward : groupB_forward;
    const bool rotatePitchDown = keyCtrl ? groupA_backward : groupB_backward;
    const bool rotateYawLeft = keyCtrl ? groupA_left : groupB_left;
    const bool rotateYawRight = keyCtrl ? groupA_right : groupB_right;

    const float shiftMultiplier = keyShift ? 3.0f : 1.0f;
    const float moveDelta = deltaTime * shiftMultiplier;
    const float rotateDelta = deltaTime * shiftMultiplier;

    const glm::vec3 oldPos = activeCamera->GetPosition();

    if (moveForward)  activeCamera->MoveForward(moveDelta);
    if (moveBackward) activeCamera->MoveBackward(moveDelta);
    if (moveLeft)     activeCamera->MoveLeft(moveDelta);
    if (moveRight)    activeCamera->MoveRight(moveDelta);
    if (moveDown)     activeCamera->MoveDown(moveDelta);
    if (moveUp)       activeCamera->MoveUp(moveDelta);

    glm::vec3 currentPos = activeCamera->GetPosition();
    // ClampCameraPosition(currentPos, scene, oldPos); // Optional for custom cams
    if (activeCameraType == CameraType::FREE_ROAM) {
        ClampCameraPosition(currentPos, scene, oldPos);
    }
    activeCamera->SetPosition(currentPos);

    if (rotatePitchUp)   activeCamera->RotatePitch(rotateDelta);
    if (rotatePitchDown) activeCamera->RotatePitch(-rotateDelta);
    if (rotateYawLeft)   activeCamera->RotateYaw(-rotateDelta);
    if (rotateYawRight)  activeCamera->RotateYaw(rotateDelta);
}

void CameraController::OnKeyPress(int key, bool pressed) {
    if (key == GLFW_KEY_W) keyW = pressed;
    if (key == GLFW_KEY_A) keyA = pressed;
    if (key == GLFW_KEY_S) keyS = pressed;
    if (key == GLFW_KEY_D) keyD = pressed;
    if (key == GLFW_KEY_Q || key == GLFW_KEY_PAGE_DOWN) keyQ = pressed;
    if (key == GLFW_KEY_E || key == GLFW_KEY_PAGE_UP) keyE = pressed;
    if (key == GLFW_KEY_UP) keyUp = pressed;
    if (key == GLFW_KEY_LEFT) keyLeft = pressed;
    if (key == GLFW_KEY_DOWN) keyDown = pressed;
    if (key == GLFW_KEY_RIGHT) keyRight = pressed;
    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL) keyCtrl = pressed;
    if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) keyShift = pressed;
}

void CameraController::ClampCameraPosition(glm::vec3& pos, Scene& scene, const glm::vec3& prevPos) const {
    const float COLLISION_BUFFER = 1.7f;

    const auto& terrain = scene.GetTerrainConfig();
    if (terrain.exists) {
        const float localX = pos.x - terrain.position.x;
        const float localZ = pos.z - terrain.position.z;
        const float distFromCenter = glm::length(glm::vec2(localX, localZ));

        if (distFromCenter < terrain.radius) {
            const float rawNoiseHeight = GeometryGenerator::GetTerrainHeight(
                localX, localZ,
                terrain.radius,
                terrain.heightScale,
                terrain.noiseFreq
            );

            const float worldFloorY = rawNoiseHeight + terrain.position.y;
            const float clampHeight = worldFloorY + COLLISION_BUFFER;

            if (pos.y < clampHeight) {
                pos.y = clampHeight;
            }
        }
    }

    Registry& registry = scene.GetRegistry();
    for (Entity e : scene.GetRenderableEntities()) {
        if (!registry.HasComponent<ColliderComponent>(e) || !registry.HasComponent<TransformComponent>(e)) continue;

        auto& collider = registry.GetComponent<ColliderComponent>(e);
        auto& transform = registry.GetComponent<TransformComponent>(e);

        if (!collider.hasCollision) continue;

        const glm::vec3 objPos = glm::vec3(transform.matrix[3]);
        const float objTop = objPos.y + collider.height;

        const float distXZ = glm::distance(glm::vec2(pos.x, pos.z), glm::vec2(objPos.x, objPos.z));
        const float minSeparation = collider.radius + COLLISION_BUFFER;

        if (distXZ < minSeparation) {
            // Move bufferedTop inside for local optimization (OPT.01)
            const float bufferedTop = objTop + COLLISION_BUFFER;
            const bool isInsideVertical = (pos.y > objPos.y) && (pos.y < bufferedTop);

            if (isInsideVertical) {
                // Move wasAbove inside for local optimization (OPT.01)
                const bool wasAbove = (prevPos.y >= bufferedTop);
                if (wasAbove) {
                    pos.y = bufferedTop;
                }
                else {
                    glm::vec2 dir = glm::vec2(pos.x, pos.z) - glm::vec2(objPos.x, objPos.z);
                    if (glm::length(dir) < 0.001f) dir = glm::vec2(1.0f, 0.0f);
                    else dir = glm::normalize(dir);

                    const glm::vec2 corrected = glm::vec2(objPos.x, objPos.z) + dir * minSeparation;
                    pos.x = corrected.x;
                    pos.z = corrected.y;
                }
            }
        }
    }
}