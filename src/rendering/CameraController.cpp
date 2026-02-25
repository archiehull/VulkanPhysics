#include "CameraController.h"
#include "Scene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <random>
#include <glm/gtc/quaternion.hpp>

CameraController::CameraController(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs) {
    SetupCameras(scene, customConfigs);

    // Switch to the first available camera by default
    if (!customConfigs.empty()) {
        SwitchCamera(customConfigs[0].name, scene);
    }
}

void CameraController::SetOrbitTarget(Entity target, Scene& scene) {
    if (target == MAX_ENTITIES) return;

    OrbitTargetObject = target;
    auto& registry = scene.GetRegistry();
    glm::vec3 targetPos = glm::vec3(registry.GetComponent<TransformComponent>(target).matrix[3]);

    // If the current camera doesn't support orbiting, automatically switch to the main outside camera
    const auto& meta = cameraMeta[activeCameraName];
    if (meta.type != "Orbit" && meta.type != "RandomTarget") {
        SwitchCamera("OutsideCam", scene);
    }

    // --- NEW: Dynamic Close-Up Radius ---
    float viewRadius = 15.0f; // A tight default fallback
    float yOffset = 3.0f;     // Default to looking slightly above the base

    // If the object has a collider, frame it nicely based on its actual size
    if (registry.HasComponent<ColliderComponent>(target)) {
        const auto& collider = registry.GetComponent<ColliderComponent>(target);

        // Make the radius ~2.5x the size of the object, ensuring a minimum distance of 10
        viewRadius = std::max(10.0f, std::max(collider.radius, collider.height) * 2.5f);

        // Look at the vertical center of the object instead of its feet
        yOffset = collider.height * 0.5f;
    }

    // Snap the camera to orbit the object at the new close-up distance
    scene.SetObjectOrbit(activeCameraName, targetPos + glm::vec3(0, yOffset, 0), viewRadius, 0.0f, { 0,1,0 }, { 1,0,0 });

    std::cout << "Camera forced to orbit entity: " << target << " at radius: " << viewRadius << std::endl;
}

void CameraController::SetupCameras(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs) {
    for (const auto& conf : customConfigs) {
        Entity camEnt = scene.CreateCameraEntity(conf.name, conf.position, conf.type);

        cameraEntities[conf.name] = camEnt;
        cameraMeta[conf.name] = conf;

        if (!conf.actionBind.empty()) {
            bindToNameMap[conf.actionBind] = conf.name;
        }
    }
}

void CameraController::SwitchCameraByBind(const std::string& actionBind, Scene& scene) {
    if (bindToNameMap.find(actionBind) != bindToNameMap.end()) {
        SwitchCamera(bindToNameMap[actionBind], scene);
    }
}

void CameraController::SwitchCamera(const std::string& name, Scene& scene) {
    if (cameraEntities.find(name) == cameraEntities.end()) return;

    auto& registry = scene.GetRegistry();

    if (activeCameraEntity != MAX_ENTITIES) {
        registry.GetComponent<CameraComponent>(activeCameraEntity).isActive = false;
    }

    activeCameraName = name;
    activeCameraEntity = cameraEntities[name];
    registry.GetComponent<CameraComponent>(activeCameraEntity).isActive = true;

    const auto& meta = cameraMeta[name];
    OrbitTargetObject = MAX_ENTITIES;

    if (meta.type == "RandomTarget") {
        std::vector<Entity> validTargets;
        for (Entity e : scene.GetRenderableEntities()) {
            if (registry.HasComponent<RenderComponent>(e)) {
                if (registry.GetComponent<RenderComponent>(e).texturePath.find(meta.targetMatch) != std::string::npos) {
                    validTargets.push_back(e);
                }
            }
        }

        if (!validTargets.empty()) {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, (int)validTargets.size() - 1);
            Entity target = validTargets[dis(gen)];

            OrbitTargetObject = target;
            glm::vec3 targetPos = glm::vec3(registry.GetComponent<TransformComponent>(target).matrix[3]);
            scene.SetObjectOrbit(name, targetPos + glm::vec3(0, 3, 0), meta.orbitRadius, 0.0f, { 0,1,0 }, { 1,0,0 });
        }
    }
    else if (meta.type == "Orbit") {
        scene.SetObjectOrbit(name, meta.target, meta.orbitRadius, 0.05f, { 0,1,0 }, { 1,0,0 });
    }

    std::cout << "Switched to Camera: " << name << std::endl;
}

void CameraController::Update(float deltaTime, Scene& scene, const InputManager& input) {
    if (activeCameraEntity == MAX_ENTITIES) return;

    const auto& meta = cameraMeta[activeCameraName];

    if (meta.type == "FreeRoam") {
        UpdateFreeRoam(deltaTime, scene, input);
    }
    else {
        UpdateOrbitInput(deltaTime, scene, input);
    }
}

void CameraController::UpdateFreeRoam(float deltaTime, Scene& scene, const InputManager& input) {
    auto& registry = scene.GetRegistry();
    auto& transform = registry.GetComponent<TransformComponent>(activeCameraEntity);
    auto& cam = registry.GetComponent<CameraComponent>(activeCameraEntity);

    // Speed constants
    bool sprinting = input.IsActionHeld(InputAction::Sprint);
    const float shiftMult = sprinting ? 3.0f : 1.0f;
    const float moveSpeed = 35.0f * shiftMult;
    const float rotateSpeed = 60.0f * shiftMult;

    // Calculate basis vectors
    glm::vec3 front = -glm::normalize(glm::vec3(transform.matrix[2])); // Z-Axis
    glm::vec3 right = glm::normalize(glm::vec3(transform.matrix[0]));  // X-Axis
    glm::vec3 up = { 0, 1, 0 };

    glm::vec3 pos = glm::vec3(transform.matrix[3]);
    glm::vec3 prevPos = pos;

    // Translation
    if (input.IsActionHeld(InputAction::MoveForward))  pos += front * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveBackward)) pos -= front * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveLeft))     pos -= right * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveRight))    pos += right * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveUp))       pos += up * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveDown))     pos -= up * moveSpeed * deltaTime;

    ClampCameraPosition(pos, scene, prevPos);

    // Rotation (Make sure to add Look actions to your InputAction enum and configs!)
    if (input.IsActionHeld(InputAction::LookLeft))  cam.yaw += rotateSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::LookRight)) cam.yaw -= rotateSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::LookUp))    cam.pitch += rotateSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::LookDown))  cam.pitch -= rotateSpeed * deltaTime;

    cam.pitch = std::clamp(cam.pitch, -89.0f, 89.0f);

    // Reconstruct Matrix from Pos + Euler
    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    m = glm::rotate(m, glm::radians(cam.yaw), { 0, 1, 0 });
    m = glm::rotate(m, glm::radians(cam.pitch), { 1, 0, 0 });
    transform.matrix = m;
}

void CameraController::UpdateOrbitInput(float deltaTime, Scene& scene, const InputManager& input) {
    auto& registry = scene.GetRegistry();
    if (!registry.HasComponent<OrbitComponent>(activeCameraEntity)) return;

    auto& orbit = registry.GetComponent<OrbitComponent>(activeCameraEntity);

    const float rotateSpeed = 50.0f;
    const float zoomSpeed = 50.0f;

    // 1. Yaw (Orbit Left/Right)
    if (input.IsActionHeld(InputAction::MoveLeft) || input.IsActionHeld(InputAction::LookLeft))  orbit.currentAngle -= glm::radians(rotateSpeed * deltaTime);
    if (input.IsActionHeld(InputAction::MoveRight) || input.IsActionHeld(InputAction::LookRight)) orbit.currentAngle += glm::radians(rotateSpeed * deltaTime);

    // 2. Zoom (Radius)
    if (input.IsActionHeld(InputAction::MoveDown)) orbit.radius -= zoomSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveUp))   orbit.radius += zoomSpeed * deltaTime;
    orbit.radius = std::max(orbit.radius, 1.0f);

    // 3. Pitch (Elevation Up/Down)
    if (input.IsActionHeld(InputAction::MoveForward) || input.IsActionHeld(InputAction::LookUp) ||
        input.IsActionHeld(InputAction::MoveBackward) || input.IsActionHeld(InputAction::LookDown)) {

        float deltaElev = 0.0f;
        if (input.IsActionHeld(InputAction::MoveBackward) || input.IsActionHeld(InputAction::LookDown)) deltaElev += glm::radians(rotateSpeed * deltaTime); // Move Up
        if (input.IsActionHeld(InputAction::MoveForward) || input.IsActionHeld(InputAction::LookUp))  deltaElev -= glm::radians(rotateSpeed * deltaTime); // Move Down

        glm::vec3 right = glm::normalize(glm::cross(orbit.axis, orbit.startVector));

        glm::quat pitchQuat = glm::angleAxis(deltaElev, right);
        glm::vec3 newStart = pitchQuat * orbit.startVector;

        float dot = glm::dot(glm::normalize(newStart), orbit.axis);
        if (glm::abs(dot) < 0.999f) {
            orbit.startVector = newStart;
        }
    }
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
            const float bufferedTop = objTop + COLLISION_BUFFER;
            const bool isInsideVertical = (pos.y > objPos.y) && (pos.y < bufferedTop);

            if (isInsideVertical) {
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