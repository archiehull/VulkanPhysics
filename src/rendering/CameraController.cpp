#include "CameraController.h"
#include "Scene.h"
#include "../geometry/GeometryGenerator.h"
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

    if (!registry.HasComponent<TransformComponent>(target)) return;

    glm::vec3 targetPos = glm::vec3(registry.GetComponent<TransformComponent>(target).matrix[3]);

    const auto& meta = cameraMeta[activeCameraName];
    if (meta.type != "Orbit" && meta.type != "RandomTarget") {
        std::string fallback;
        for (const auto& [camName, camMeta] : cameraMeta) {
            if (camMeta.type == "Orbit" || camMeta.type == "RandomTarget") {
                fallback = camName;
                break;
            }
        }
        if (!fallback.empty()) {
            SwitchCamera(fallback, scene);
        }
    }

    float viewRadius = 15.0f;
    float yOffset = 0.0f;

    auto& targetTransform = registry.GetComponent<TransformComponent>(target);
    const float maxScale = std::max({ targetTransform.scale.x, targetTransform.scale.y, targetTransform.scale.z });

    if (registry.HasComponent<ColliderComponent>(target)) {
        auto& collider = registry.GetComponent<ColliderComponent>(target);
        if (collider.type == 1) {
            viewRadius = std::max(maxScale * 4.0f, 8.0f);
            yOffset = maxScale * 0.5f;
        }
        else {
            viewRadius = std::clamp(std::max(collider.radius * 3.0f, 5.0f), 5.0f, 60.0f);
            yOffset = std::max(collider.height * 0.5f, maxScale * 0.5f);
        }
    }
    else {
        viewRadius = std::max(maxScale * 4.0f, 5.0f);
        yOffset = maxScale * 0.5f;
    }

    targetPos.y += yOffset;

    if (!registry.HasComponent<TransformComponent>(activeCameraEntity) ||
        !registry.HasComponent<CameraComponent>(activeCameraEntity)) {
        return;
    }

    auto& camTransform = registry.GetComponent<TransformComponent>(activeCameraEntity);
    auto& camComp = registry.GetComponent<CameraComponent>(activeCameraEntity);

    glm::vec3 offset = glm::vec3(0.0f, viewRadius * 0.5f, viewRadius);
    glm::vec3 newCameraPosition = targetPos + offset;

    camTransform.position = newCameraPosition;

    glm::vec3 direction = glm::normalize(targetPos - newCameraPosition);
    camComp.yaw = glm::degrees(atan2(direction.z, direction.x));
    camComp.pitch = glm::degrees(asin(direction.y));

    camTransform.rotation.x = camComp.pitch;
    camTransform.rotation.y = camComp.yaw;
    camTransform.rotation.z = 0.0f;

    if (registry.HasComponent<OrbitComponent>(activeCameraEntity)) {
        auto& orbitComp = registry.GetComponent<OrbitComponent>(activeCameraEntity);
        if (orbitComp.isOrbiting) {
            orbitComp.center = targetPos;
            orbitComp.radius = viewRadius;
            orbitComp.speed = 0.0f;

            glm::vec3 flatOffset = glm::vec3(newCameraPosition.x - targetPos.x, 0.0f, newCameraPosition.z - targetPos.z);
            if (glm::length(flatOffset) > 0.001f) {
                orbitComp.startVector = glm::normalize(flatOffset) * viewRadius;
            }
            orbitComp.currentAngle = 0.0f;
        }
    }

    camTransform.UpdateMatrix();

    // NEW: Print current orbit target name
    if (registry.HasComponent<NameComponent>(target)) {
        std::cout << "[OrbitTarget] " << registry.GetComponent<NameComponent>(target).name << std::endl;
    }
    else {
        std::cout << "[OrbitTarget] Entity " << target << std::endl;
    }
}

void CameraController::SetupCameras(Scene& scene, const std::vector<CustomCameraConfig>& customConfigs) {
    for (const auto& conf : customConfigs) {
        Entity camEnt = scene.CreateCameraEntity(conf.name, conf.position, conf.type);

        if (conf.type == "FreeRoam" && scene.GetRegistry().HasComponent<CameraComponent>(camEnt)) {
            auto& camComp = scene.GetRegistry().GetComponent<CameraComponent>(camEnt);
            camComp.yaw = conf.yaw;
            camComp.pitch = conf.pitch;
        }

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

bool CameraController::IsActiveCameraBoundTo(const std::string& actionBind) const {
    auto it = bindToNameMap.find(actionBind);
    if (it == bindToNameMap.end()) return false;
    return it->second == activeCameraName;
}

void CameraController::CycleRandomTarget(Scene& scene) {
    if (activeCameraEntity == MAX_ENTITIES) return;

    auto it = cameraMeta.find(activeCameraName);
    if (it == cameraMeta.end()) return;
    const auto& meta = it->second;

    if (meta.type != "RandomTarget") return;

    BuildRandomTargetCycle(scene, meta);
    if (randomTargetCycle.empty()) return;

    auto found = std::find(randomTargetCycle.begin(), randomTargetCycle.end(), OrbitTargetObject);
    if (found != randomTargetCycle.end()) {
        randomTargetIndex = (static_cast<std::size_t>(std::distance(randomTargetCycle.begin(), found)) + 1) % randomTargetCycle.size();
    }
    else {
        randomTargetIndex = 0;
    }

    ApplyRandomTarget(scene, randomTargetCycle[randomTargetIndex]);
}

void CameraController::BuildRandomTargetCycle(Scene& scene, const CustomCameraConfig& meta) {
    randomTargetCycle.clear();

    auto& registry = scene.GetRegistry();

    for (Entity e : scene.GetRenderableEntities()) {
        if (!registry.HasComponent<RenderComponent>(e) || !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        const auto& render = registry.GetComponent<RenderComponent>(e);

        // Optional: skip hidden objects
        if (!render.visible) {
            continue;
        }

        // Skip generated shadow helper entities
        if (registry.HasComponent<NameComponent>(e)) {
            const std::string& n = registry.GetComponent<NameComponent>(e).name;
            if (n.size() >= 7 && n.rfind("_Shadow") == n.size() - 7) {
                continue;
            }
        }

        // IMPORTANT: do NOT filter by targetMatch
        randomTargetCycle.push_back(e);
    }

    if (randomTargetCycle.empty()) {
        OrbitTargetObject = MAX_ENTITIES;
        return;
    }

    if (randomTargetIndex >= randomTargetCycle.size()) {
        randomTargetIndex = 0;
    }
}
void CameraController::ApplyRandomTarget(Scene& scene, Entity target) {
    if (target == MAX_ENTITIES) return;
    SetOrbitTarget(target, scene);
}

void CameraController::SwitchCamera(const std::string& name, Scene& scene) {
    if (cameraEntities.find(name) == cameraEntities.end()) return;

    // Prevent reinitializing same camera on repeated key press
    if (activeCameraEntity != MAX_ENTITIES && activeCameraName == name) {
        return;
    }

    auto& registry = scene.GetRegistry();

    if (activeCameraEntity != MAX_ENTITIES) {
        if (registry.HasComponent<CameraComponent>(activeCameraEntity)) {
            registry.GetComponent<CameraComponent>(activeCameraEntity).isActive = false;
        }
    }

    activeCameraName = name;
    activeCameraEntity = cameraEntities[name];
    if (!registry.HasComponent<CameraComponent>(activeCameraEntity)) {
        return;
    }
    registry.GetComponent<CameraComponent>(activeCameraEntity).isActive = true;

    const auto& meta = cameraMeta[name];
    OrbitTargetObject = MAX_ENTITIES;

    randomTargetCycle.clear();
    randomTargetIndex = 0;
    randomTargetSwitchTimer = 0.0f;

    if (meta.type == "RandomTarget") {
        BuildRandomTargetCycle(scene, meta);
        if (!randomTargetCycle.empty()) {
            ApplyRandomTarget(scene, randomTargetCycle[randomTargetIndex]);
        }
    }
    else if (meta.type == "Orbit") {
        scene.SetObjectOrbit(name, meta.target, meta.orbitRadius, 0.0f, { 0,1,0 }, { 1,0,0 }); // <-- no default movement
    }

    std::cout << "Switched to Camera: " << name << std::endl;
}

void CameraController::Update(float deltaTime, Scene& scene, const InputManager& input) {
    if (activeCameraEntity == MAX_ENTITIES) return;

    auto& registry = scene.GetRegistry();
    const auto& meta = cameraMeta[activeCameraName];

    // No auto-cycling; only recover if target disappears
    if (meta.type == "RandomTarget") {
        const bool lostTarget =
            (OrbitTargetObject == MAX_ENTITIES) ||
            !registry.HasComponent<TransformComponent>(OrbitTargetObject);

        if (lostTarget) {
            BuildRandomTargetCycle(scene, meta);
            if (!randomTargetCycle.empty()) {
                if (randomTargetIndex >= randomTargetCycle.size()) randomTargetIndex = 0;
                ApplyRandomTarget(scene, randomTargetCycle[randomTargetIndex]);
            }
        }
    }

    if (OrbitTargetObject != MAX_ENTITIES) {
        if (registry.HasComponent<TransformComponent>(OrbitTargetObject) &&
            registry.HasComponent<OrbitComponent>(activeCameraEntity)) {

            glm::vec3 targetPos = glm::vec3(registry.GetComponent<TransformComponent>(OrbitTargetObject).matrix[3]);

            float yOffset = 3.0f;
            if (registry.HasComponent<ColliderComponent>(OrbitTargetObject)) {
                yOffset = registry.GetComponent<ColliderComponent>(OrbitTargetObject).height * 0.5f;
            }

            registry.GetComponent<OrbitComponent>(activeCameraEntity).center = targetPos + glm::vec3(0.0f, yOffset, 0.0f);
        }
    }

    if (meta.type == "FreeRoam") {
        UpdateFreeRoam(deltaTime, scene, input);
    }
    else {
        UpdateOrbitInput(deltaTime, scene, input);
    }
}

void CameraController::UpdateFreeRoam(float deltaTime, Scene& scene, const InputManager& input) {
    if (activeCameraEntity == MAX_ENTITIES) return;

    auto& registry = scene.GetRegistry();
    if (!registry.HasComponent<CameraComponent>(activeCameraEntity) ||
        !registry.HasComponent<TransformComponent>(activeCameraEntity)) return;

    auto& cam = registry.GetComponent<CameraComponent>(activeCameraEntity);
    auto& transform = registry.GetComponent<TransformComponent>(activeCameraEntity);

    bool sprinting = input.IsActionHeld(InputAction::Sprint);
    const float shiftMult = sprinting ? 3.0f : 1.0f;
    const float moveSpeed = cam.moveSpeed * shiftMult;
    const float rotateSpeed = cam.rotateSpeed * shiftMult;

    glm::vec3 front = -glm::normalize(glm::vec3(transform.matrix[2]));
    glm::vec3 right = glm::normalize(glm::vec3(transform.matrix[0]));
    glm::vec3 up = { 0, 1, 0 };

    glm::vec3 pos = glm::vec3(transform.matrix[3]);
    glm::vec3 prevPos = pos;

    if (input.IsActionHeld(InputAction::MoveForward))  pos += front * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveBackward)) pos -= front * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveLeft))     pos -= right * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveRight))    pos += right * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveUp))       pos += up * moveSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::MoveDown))     pos -= up * moveSpeed * deltaTime;

    bool isNoclip = cam.noclipEnabled;
    if (!isNoclip) {
        ClampCameraPosition(pos, scene, prevPos);
    }

    if (input.IsActionHeld(InputAction::LookLeft))  cam.yaw += rotateSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::LookRight)) cam.yaw -= rotateSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::LookUp))    cam.pitch += rotateSpeed * deltaTime;
    if (input.IsActionHeld(InputAction::LookDown))  cam.pitch -= rotateSpeed * deltaTime;

    cam.pitch = std::clamp(cam.pitch, -89.0f, 89.0f);

    // IMPORTANT: write back source-of-truth fields before UpdateMatrix()
    transform.position = pos;
    transform.rotation = glm::vec3(cam.pitch, cam.yaw, 0.0f);
    transform.UpdateMatrix();
}

void CameraController::UpdateOrbitInput(float deltaTime, Scene& scene, const InputManager& input) {
    auto& registry = scene.GetRegistry();
    auto& cam = registry.GetComponent<CameraComponent>(activeCameraEntity);
    auto& orbit = registry.GetComponent<OrbitComponent>(activeCameraEntity);

    const float rotateSpeed = cam.rotateSpeed;
    const float zoomSpeed = cam.moveSpeed;

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