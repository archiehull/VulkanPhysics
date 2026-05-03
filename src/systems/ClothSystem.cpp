#include "ClothSystem.h"
#include "../core/Components.h"
#include <glm/glm.hpp>

void ClothSystem::Update(Scene& scene, float deltaTime) {
    auto& registry = scene.GetRegistry();
    auto clothArray = registry.GetComponentArray<ClothComponent>();
    auto transformArray = registry.GetComponentArray<TransformComponent>();

    const Entity entityCount = registry.GetEntityCount();
    for (Entity e = 0; e < entityCount; ++e) {
        if (!clothArray->HasData(e)) continue;

        auto& cloth = clothArray->GetData(e);
        if (!cloth.dynamicGeometry) continue;

        bool updated = false;

        glm::mat4 invTransform = glm::mat4(1.0f);
        if (transformArray->HasData(e)) {
            invTransform = glm::inverse(transformArray->GetData(e).matrix);
        }

        for (size_t i = 0; i < cloth.particles.size(); ++i) {
            Entity p = cloth.particles[i];
            if (p != MAX_ENTITIES && transformArray->HasData(p)) {
                const glm::vec3& worldPos = transformArray->GetData(p).position;
                if (i < cloth.dynamicGeometry->VertexCount()) {
                    glm::vec4 localPos = invTransform * glm::vec4(worldPos, 1.0f);
                    cloth.dynamicGeometry->GetVertex(i).pos = glm::vec3(localPos);
                    updated = true;
                }
            }
        }

        if (updated) {
            // Recalculate normals
            if (cloth.dynamicGeometry->HasIndices()) {
                // reset normals
                for(size_t i = 0; i < cloth.dynamicGeometry->VertexCount(); ++i) {
                    cloth.dynamicGeometry->GetVertex(i).normal = glm::vec3(0.0f);
                }
                const auto& indices = cloth.dynamicGeometry->GetIndices();
                for(size_t i = 0; i < indices.size(); i += 3) {
                    uint32_t i0 = indices[i];
                    uint32_t i1 = indices[i+1];
                    uint32_t i2 = indices[i+2];
                    glm::vec3 v0 = cloth.dynamicGeometry->GetVertex(i0).pos;
                    glm::vec3 v1 = cloth.dynamicGeometry->GetVertex(i1).pos;
                    glm::vec3 v2 = cloth.dynamicGeometry->GetVertex(i2).pos;
                    glm::vec3 normal = glm::cross(v1 - v0, v2 - v0);
                    cloth.dynamicGeometry->GetVertex(i0).normal += normal;
                    cloth.dynamicGeometry->GetVertex(i1).normal += normal;
                    cloth.dynamicGeometry->GetVertex(i2).normal += normal;
                }
                for(size_t i = 0; i < cloth.dynamicGeometry->VertexCount(); ++i) {
                    glm::vec3& n = cloth.dynamicGeometry->GetVertex(i).normal;
                    float lenSq = glm::dot(n, n);
                    if (lenSq > 1e-8f) {
                        n *= glm::inversesqrt(lenSq);
                    } else {
                        n = glm::vec3(0.0f, 1.0f, 0.0f);
                    }
                }
            }

            cloth.dynamicGeometry->UpdateVertexBuffer();
        }
    }
}
