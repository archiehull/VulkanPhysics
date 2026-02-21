#include "OrbitSystem.h"
#include <glm/gtc/quaternion.hpp>

void OrbitSystem::Update(Registry& registry, float deltaTime) {
    // Iterate over EVERY entity in existence to check for orbits
    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {

        // Skip entities that don't have the required components
        if (!registry.HasComponent<OrbitComponent>(e) || !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        auto& orbit = registry.GetComponent<OrbitComponent>(e);
        if (orbit.isOrbiting) {
            auto& transform = registry.GetComponent<TransformComponent>(e);

            // Calculate new position
            orbit.currentAngle += orbit.speed * deltaTime;
            const glm::quat rotation = glm::angleAxis(orbit.currentAngle, orbit.axis);
            const glm::vec3 offset = rotation * orbit.startVector;

            // Apply to transform matrix (translation is the 4th column)
            transform.matrix[3] = glm::vec4(orbit.center + offset, 1.0f);
        }
    }
}