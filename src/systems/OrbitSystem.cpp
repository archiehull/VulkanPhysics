#include "OrbitSystem.h"
#include "../rendering/Scene.h"
#include <glm/gtc/quaternion.hpp>

void OrbitSystem::Update(Scene& scene, float deltaTime) {
    Registry& registry = scene.GetRegistry();

    auto orbitArray = registry.GetComponentArray<OrbitComponent>();
    auto transformArray = registry.GetComponentArray<TransformComponent>();

    const size_t orbitCount = orbitArray->GetSize();
    for (size_t idx = 0; idx < orbitCount; ++idx) {
        Entity e = orbitArray->GetEntityAtIndex(idx);
        if (e == MAX_ENTITIES || !transformArray->HasData(e)) {
            continue;
        }

        auto& orbit = orbitArray->GetData(e);
        if (orbit.isOrbiting) {
            auto& transform = transformArray->GetData(e);

            orbit.currentAngle += orbit.speed * deltaTime;
            const glm::quat rotation = glm::angleAxis(orbit.currentAngle, orbit.axis);
            glm::vec3 direction = glm::normalize(orbit.startVector);
            const glm::vec3 offset = rotation * direction * orbit.radius;
            transform.position = orbit.center + offset;
            transform.UpdateMatrix();
        }
    }
}