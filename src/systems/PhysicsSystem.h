#pragma once

#include "ISystem.h"
#include "../core/ECS.h"

struct TransformComponent;
class Plane;

class PhysicsSystem : public ISystem {
public:
    void Update(Scene& scene, float deltaTime) override;

private:
    void Integrate(Registry& registry, float dt);
    void ResolveCollisions(Registry& registry);
    bool IsCollidable(const Registry& reg, Entity e);
    void ApplyPositionCorrection(struct TransformComponent& t1, struct TransformComponent& t2, float r1, float r2, bool isStatic1, bool isStatic2);
    void ApplySpherePlaneCorrection(TransformComponent& sphereTrans, float radius, const Plane& plane);
};