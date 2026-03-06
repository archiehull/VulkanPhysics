#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "CameraSystem.h"
#include "../rendering/Scene.h"
#include <glm/gtc/matrix_transform.hpp>

void CameraSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<CameraComponent>(e) || !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        auto& cam = registry.GetComponent<CameraComponent>(e);
        auto& transform = registry.GetComponent<TransformComponent>(e);

        // FIX: Read directly from the position vector, not the matrix!
        const glm::vec3 pos = transform.position;

        // 1. Calculate View Matrix
        if (registry.HasComponent<OrbitComponent>(e) && registry.GetComponent<OrbitComponent>(e).isOrbiting) {
            const auto& orbit = registry.GetComponent<OrbitComponent>(e);
            cam.viewMatrix = glm::lookAt(pos, orbit.center, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        else {
            // Ensure the matrix is up-to-date before we invert it for the camera view
            transform.UpdateMatrix();
            cam.viewMatrix = glm::inverse(transform.matrix);
        }

        // 2. Calculate Projection Matrix
        const float aspect = (cam.aspectRatio > 0.0f) ? cam.aspectRatio : 1.0f;
        cam.projectionMatrix = glm::perspective(glm::radians(cam.fov), aspect, cam.nearPlane, cam.farPlane);
        cam.projectionMatrix[1][1] *= -1; // Vulkan Y-flip

        // 3. Layer Mask / Region Logic
        int targetViewMask = SceneLayers::LAYER_A; // Default fallback to Base World

        for (Entity regionEnt = 0; regionEnt < registry.GetEntityCount(); ++regionEnt) {
            if (!registry.HasComponent<LayerRegionComponent>(regionEnt) || !registry.HasComponent<TransformComponent>(regionEnt)) {
                continue;
            }

            auto& region = registry.GetComponent<LayerRegionComponent>(regionEnt);
            auto& regTransform = registry.GetComponent<TransformComponent>(regionEnt);
            glm::vec3 regionPos = regTransform.position;

            bool isInsideRegion = false;

            if (region.volumeType == 0) { // SPHERE CHECK
                float dist = glm::distance(pos, regionPos);
                if (dist <= region.radius) isInsideRegion = true;
            }
            else if (region.volumeType == 1) { // AABB (BOX) CHECK
                glm::vec3 minBounds = regionPos - region.halfExtents;
                glm::vec3 maxBounds = regionPos + region.halfExtents;

                if (pos.x >= minBounds.x && pos.x <= maxBounds.x &&
                    pos.y >= minBounds.y && pos.y <= maxBounds.y &&
                    pos.z >= minBounds.z && pos.z <= maxBounds.z) {
                    isInsideRegion = true;
                }
            }

            // If we are inside this layer entity's bounds, switch our view to its layer!
            if (isInsideRegion) {
                targetViewMask = (1 << region.assignedLayerBit);
                break;
            }
        }

        cam.viewMask = targetViewMask;
    }
}

bool CameraSystem::IsNoclip(Scene& scene, Entity cameraEntity) {
    auto& registry = scene.GetRegistry();
    Entity target = cameraEntity;

    if (target == MAX_ENTITIES) {
        for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
            if (registry.HasComponent<CameraComponent>(e) && registry.GetComponent<CameraComponent>(e).isActive) {
                target = e;
                break;
            }
        }
    }

    if (target != MAX_ENTITIES && registry.HasComponent<CameraComponent>(target)) {
        return registry.GetComponent<CameraComponent>(target).noclipEnabled;
    }
    return false;
}

void CameraSystem::SetNoclip(Scene& scene, bool noclipEnabled, Entity cameraEntity) {
    auto& registry = scene.GetRegistry();
    Entity target = cameraEntity;

    if (target == MAX_ENTITIES) {
        for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
            if (registry.HasComponent<CameraComponent>(e) && registry.GetComponent<CameraComponent>(e).isActive) {
                target = e;
                break;
            }
        }
    }

    if (target != MAX_ENTITIES && registry.HasComponent<CameraComponent>(target)) {
        registry.GetComponent<CameraComponent>(target).noclipEnabled = noclipEnabled;
    }
}

void CameraSystem::ToggleNoclip(Scene& scene, Entity cameraEntity) {
    SetNoclip(scene, !IsNoclip(scene, cameraEntity), cameraEntity);
}