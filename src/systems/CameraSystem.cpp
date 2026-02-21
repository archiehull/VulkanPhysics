#include "CameraSystem.h"
#include "../rendering/Scene.h"
#include <glm/gtc/matrix_transform.hpp>

void CameraSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();

    // In Vulkan, we need the aspect ratio for the projection matrix
    // We'll assume the Scene or a Global state provides this. 
    // For now, let's assume a standard 16:9 or fetch it from scene data.
    float aspectRatio = 16.0f / 9.0f;

    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<CameraComponent>(e) || !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        auto& cam = registry.GetComponent<CameraComponent>(e);
        auto& transform = registry.GetComponent<TransformComponent>(e);

        //// 1. Calculate View Matrix (Inverse of World Transform)
        //// Extract position and orientation from the transform matrix
        //glm::vec3 pos = glm::vec3(transform.matrix[3]);

        //// If we want to support the old "Front/Up" style, we derive them from the matrix
        //glm::vec3 front = glm::normalize(glm::vec3(transform.matrix[2])); // Forward is usually -Z or Z
        //glm::vec3 up = glm::normalize(glm::vec3(transform.matrix[1]));

        //cam.viewMatrix = glm::lookAt(pos, pos + front, up);

        // 1. Calculate View Matrix
        // The View Matrix is simply the inverse of the Camera's World Transform
        cam.viewMatrix = glm::inverse(transform.matrix);

        // 2. Calculate Projection Matrix
        cam.projectionMatrix = glm::perspective(glm::radians(cam.fov), aspectRatio, cam.nearPlane, cam.farPlane);
        cam.projectionMatrix[1][1] *= -1; // Vulkan Y-flip
    }
}