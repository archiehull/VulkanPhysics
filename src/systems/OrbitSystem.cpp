#include "OrbitSystem.h"
#include "../rendering/Scene.h"
#include <glm/gtc/quaternion.hpp>

void OrbitSystem::Update(Scene& scene, float deltaTime) {
    Registry& registry = scene.GetRegistry();

    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
        if (!registry.HasComponent<OrbitComponent>(e) || !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        auto& orbit = registry.GetComponent<OrbitComponent>(e);
        if (orbit.isOrbiting) {
            auto& transform = registry.GetComponent<TransformComponent>(e);

            orbit.currentAngle += orbit.speed * deltaTime;
            const glm::quat rotation = glm::angleAxis(orbit.currentAngle, orbit.axis);
            glm::vec3 direction = glm::normalize(orbit.startVector);
            const glm::vec3 offset = rotation * direction * orbit.radius;
            transform.position = orbit.center + offset;
            transform.UpdateMatrix();
        }
    }
}