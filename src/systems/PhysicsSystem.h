// src/systems/PhysicsSystem.h
#pragma once

#include "ISystem.h"
#include "../core/ECS.h"

enum class IntegrationMethod {
    ExplicitEuler,
    SemiImplicitEuler
};

class PhysicsSystem : public ISystem {
public:
    static int subSteps;
    static IntegrationMethod currentMethod;

    static bool applyGravity;

    void Update(Scene& scene, float deltaTime) override;

private:
    void Integrate(Registry& registry, float dt);
    void ResolveCollisions(Registry& registry);
    bool IsCollidable(const Registry& reg, Entity e);
    void ApplyPositionCorrection(struct TransformComponent& t1, struct TransformComponent& t2, float r1, float r2, bool static1, bool static2);
    void ApplySpherePlaneCorrection(struct TransformComponent& sphereTrans, float radius, const class Plane& plane);
};