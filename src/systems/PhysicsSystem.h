#pragma once

#include "ISystem.h"
#include "../core/ECS.h"

enum class IntegrationMethod {
    ExplicitEuler,
    SemiImplicitEuler,
    RK4
};

enum class ResolutionMethod {
    Impulse,
    Force
};

class PhysicsSystem : public ISystem {
public:
    static int subSteps;
    static IntegrationMethod currentMethod;
    static ResolutionMethod currentResolutionMethod;
    static bool applyGravity;

    // Global tuning knobs
    static float contactFrictionScale;     // Multiplies per-object friction
    static float sleepNormalThreshold;     // m/s along contact normal
    static float sleepTangentialThreshold; // m/s tangent to contact plane

    void Update(Scene& scene, float deltaTime) override;

private:
    void Integrate(Registry& registry, float dt);
    void ResolveCollisions(Registry& registry, float dt);
    bool IsCollidable(const Registry& reg, Entity e);
    void ApplyPositionCorrection(struct TransformComponent& t1, struct TransformComponent& t2, float r1, float r2, bool static1, bool static2);
    void ApplySpherePlaneCorrection(struct TransformComponent& sphereTrans, float radius, const class Plane& plane);
};