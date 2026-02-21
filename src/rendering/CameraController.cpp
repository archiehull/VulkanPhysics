#include "CameraController.h"
#include "Scene.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <random>
#include "../core/Config.h"
#include "../systems/CameraSystem.h"
#include "Camera.h"
#include <glm/gtc/quaternion.hpp>

CameraController::CameraController(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs) {
    SetupCameras(scene, customConfigs);
    SwitchCamera(CameraType::OUTSIDE_ORB, scene);
}

void CameraController::SetupCameras(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs) {
    // 1. Create Standard Entities via Scene Helper
    cameraEntities[CameraType::FREE_ROAM] = scene.CreateCameraEntity("FreeRoamCam", { 0.0f, -75.0f, 0.0f }, CameraType::FREE_ROAM);
    cameraEntities[CameraType::OUTSIDE_ORB] = scene.CreateCameraEntity("OutsideCam", { 0.0f, 60.0f, 350.0f }, CameraType::OUTSIDE_ORB);
    cameraEntities[CameraType::CACTI] = scene.CreateCameraEntity("CactusCam", { 20.0f, 10.0f, 20.0f }, CameraType::CACTI);

    // 2. Setup Custom Cameras from Config
    CameraType customTypes[] = { CameraType::CUSTOM_1, CameraType::CUSTOM_2, CameraType::CUSTOM_3, CameraType::CUSTOM_4 };
    for (size_t i = 0; i < customConfigs.size() && i < 4; ++i) {
        const auto& conf = customConfigs[i];
        CameraType type = customTypes[i];

        // Map "Orbit" string to the helper logic
        CameraType internalRefType = (conf.type == "Orbit") ? CameraType::OUTSIDE_ORB : CameraType::FREE_ROAM;
        Entity camEnt = scene.CreateCameraEntity(conf.name, conf.position, internalRefType);

        cameraEntities[type] = camEnt;

        CustomCameraInfo info;
        info.name = conf.name;
        info.type = conf.type;
        info.initialTarget = conf.target;
        customCameraMeta[type] = info;
    }
}

void CameraController::SwitchCamera(CameraType type, Scene& scene) {
    if (cameraEntities.find(type) == cameraEntities.end()) return;

    auto& registry = scene.GetRegistry();

    // Deactivate old camera component
    if (activeCameraEntity != MAX_ENTITIES) {
        registry.GetComponent<CameraComponent>(activeCameraEntity).isActive = false;
    }

    activeCameraType = type;
    activeCameraEntity = cameraEntities[type];

    // Activate new camera component
    auto& camComp = registry.GetComponent<CameraComponent>(activeCameraEntity);
    camComp.isActive = true;

    // Logic for specific modes
    if (type == CameraType::CACTI) {
        // Find a random cactus
        std::vector<Entity> cacti;
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
            std::uniform_int_distribution<> dis(0, (int)cacti.size() - 1);
            Entity target = cacti[dis(gen)];

            OrbitTargetObject = target;

            glm::vec3 targetPos = glm::vec3(registry.GetComponent<TransformComponent>(target).matrix[3]);
            scene.SetObjectOrbit("CactusCam", targetPos + glm::vec3(0, 3, 0), 15.0f, 0.0f, { 0,1,0 }, { 1,0,0 });
        }
    }
    else if (type == CameraType::OUTSIDE_ORB) {
        scene.SetObjectOrbit("OutsideCam", { 0,0,0 }, 350.0f, 0.05f, { 0,1,0 }, { 1,0,0 });
    }

    std::cout << "Switched to Camera Entity: " << registry.GetComponent<NameComponent>(activeCameraEntity).name << std::endl;
}

void CameraController::Update(float deltaTime, Scene& scene) {
    if (activeCameraEntity == MAX_ENTITIES) return;

    // 1. Handle Free Roam Input (Direct Transform Manipulation)
    if (activeCameraType == CameraType::FREE_ROAM ||
        (customCameraMeta.count(activeCameraType) && customCameraMeta[activeCameraType].type == "FreeRoam")) {
        UpdateFreeRoam(deltaTime, scene);
    }
    // 2. Handle Orbit Input (OrbitComponent Manipulation)
    else if (activeCameraType == CameraType::CACTI || activeCameraType == CameraType::OUTSIDE_ORB ||
        (customCameraMeta.count(activeCameraType) && customCameraMeta[activeCameraType].type == "Orbit")) {
        UpdateOrbitInput(deltaTime, scene);
    }
}

void CameraController::UpdateFreeRoam(float deltaTime, Scene& scene) {
    auto& registry = scene.GetRegistry();
    auto& transform = registry.GetComponent<TransformComponent>(activeCameraEntity);
    auto& cam = registry.GetComponent<CameraComponent>(activeCameraEntity);

    // Speed constants
    const float shiftMult = keyShift ? 3.0f : 1.0f;
    const float moveSpeed = 35.0f * shiftMult;
    const float rotateSpeed = 60.0f * shiftMult;

    // Calculate basis vectors from the current transform matrix
    glm::vec3 front = -glm::normalize(glm::vec3(transform.matrix[2])); // Z-Axis
    glm::vec3 right = glm::normalize(glm::vec3(transform.matrix[0]));  // X-Axis
    glm::vec3 up = { 0, 1, 0 };

    glm::vec3 pos = glm::vec3(transform.matrix[3]);
    glm::vec3 prevPos = pos;

    // Translation
    if (keyW) pos += front * moveSpeed * deltaTime;
    if (keyS) pos -= front * moveSpeed * deltaTime;
    if (keyA) pos -= right * moveSpeed * deltaTime;
    if (keyD) pos += right * moveSpeed * deltaTime;
    if (keyE) pos += up * moveSpeed * deltaTime;
    if (keyQ) pos -= up * moveSpeed * deltaTime;

    ClampCameraPosition(pos, scene, prevPos);

    // Rotation (Update Yaw/Pitch in Component)
    if (keyLeft)  cam.yaw += rotateSpeed * deltaTime;
    if (keyRight) cam.yaw -= rotateSpeed * deltaTime;
    if (keyUp)    cam.pitch += rotateSpeed * deltaTime;
    if (keyDown)  cam.pitch -= rotateSpeed * deltaTime;
    cam.pitch = std::clamp(cam.pitch, -89.0f, 89.0f);

    // Reconstruct Matrix from Pos + Euler
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    m = glm::rotate(m, glm::radians(cam.yaw), { 0, 1, 0 });
    m = glm::rotate(m, glm::radians(cam.pitch), { 1, 0, 0 });
    transform.matrix = m;
}

void CameraController::UpdateOrbitInput(float deltaTime, Scene& scene) {
    auto& registry = scene.GetRegistry();
    if (!registry.HasComponent<OrbitComponent>(activeCameraEntity)) return;

    auto& orbit = registry.GetComponent<OrbitComponent>(activeCameraEntity);

    // Using your exact old speeds
    const float rotateSpeed = 50.0f;
    const float zoomSpeed = 50.0f;

    // 1. Yaw (Orbit Left/Right) -> A & D
    if (keyA || keyLeft)  orbit.currentAngle -= glm::radians(rotateSpeed * deltaTime);
    if (keyD || keyRight) orbit.currentAngle += glm::radians(rotateSpeed * deltaTime);

    // 2. Zoom (Radius) -> Q & E
    if (keyQ)    orbit.radius -= zoomSpeed * deltaTime;
    if (keyE)  orbit.radius += zoomSpeed * deltaTime;
    orbit.radius = std::max(orbit.radius, 1.0f);

    // 3. Pitch (Elevation Up/Down) -> Q & E
    if (keyW || keyUp || keyS || keyDown) {
        float deltaElev = 0.0f;
        if (keyS || keyDown) deltaElev += glm::radians(rotateSpeed * deltaTime); // Move Up
        if (keyW || keyUp) deltaElev -= glm::radians(rotateSpeed * deltaTime); // Move Down

        // Calculate the local "Right" axis to pitch the startVector up and down
        glm::vec3 right = glm::normalize(glm::cross(orbit.axis, orbit.startVector));

        // Apply the pitch rotation to the start vector
        glm::quat pitchQuat = glm::angleAxis(deltaElev, right);
        glm::vec3 newStart = pitchQuat * orbit.startVector;

        // Prevent flipping over the top/bottom poles (like std::clamp(OrbitPitch, -89, 89))
        float dot = glm::dot(glm::normalize(newStart), orbit.axis);
        if (glm::abs(dot) < 0.999f) {
            orbit.startVector = newStart;
        }
    }
}

void CameraController::OnKeyPress(int key, bool pressed) {
    if (key == GLFW_KEY_W) keyW = pressed;
    if (key == GLFW_KEY_A) keyA = pressed;
    if (key == GLFW_KEY_S) keyS = pressed;
    if (key == GLFW_KEY_D) keyD = pressed;
    if (key == GLFW_KEY_Q) keyQ = pressed;
    if (key == GLFW_KEY_E) keyE = pressed;
    if (key == GLFW_KEY_UP) keyUp = pressed;
    if (key == GLFW_KEY_DOWN) keyDown = pressed;
    if (key == GLFW_KEY_LEFT) keyLeft = pressed;
    if (key == GLFW_KEY_RIGHT) keyRight = pressed;
    if (key == GLFW_KEY_LEFT_SHIFT) keyShift = pressed;
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