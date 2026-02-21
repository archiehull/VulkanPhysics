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

        const glm::vec3 pos = glm::vec3(transform.matrix[3]);

        // 1. Calculate View Matrix
        if (registry.HasComponent<OrbitComponent>(e) && registry.GetComponent<OrbitComponent>(e).isOrbiting) {
            const auto& orbit = registry.GetComponent<OrbitComponent>(e);
            cam.viewMatrix = glm::lookAt(pos, orbit.center, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        else {
            cam.viewMatrix = glm::inverse(transform.matrix);
        }

        // 2. Calculate Projection Matrix
        const float aspect = (cam.aspectRatio > 0.0f) ? cam.aspectRatio : 1.0f;
        cam.projectionMatrix = glm::perspective(glm::radians(cam.fov), aspect, cam.nearPlane, cam.farPlane);
        cam.projectionMatrix[1][1] *= -1; // Vulkan Y-flip
    }
}