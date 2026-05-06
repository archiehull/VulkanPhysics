#pragma once
#include "ISystem.h"
#include <glm/glm.hpp>

class FlockSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;
    bool IsPhysics() const override { return true; }

private:
    glm::vec3 Limit(const glm::vec3& v, float max) const;
    glm::vec3 Wraparound(glm::vec3 pos, const glm::vec3& minBounds, const glm::vec3& maxBounds) const;
};