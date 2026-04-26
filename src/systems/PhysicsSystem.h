#pragma once

#include "ISystem.h"
#include "../core/ECS.h"
#include <glm/glm.hpp>

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
    static float gravityDirection;

    // Global tuning knobs
    static float contactFrictionScale;     // Multiplies per-object friction
    static float sleepNormalThreshold;     // m/s along contact normal
    static float sleepTangentialThreshold; // m/s tangent to contact plane

    // Drag / damping controls (opt-in)
    static bool applyLinearDamping;
    static float linearDampingFactor;      // [0..1], 1 = no damping
    static bool applyQuadraticDrag;
    static float quadraticDragCoefficient; // ~0.0f..1.0f depending scale

    static bool simulationPaused;

    // Setters for runtime adjustment
    static void SetLinearDamping(bool enabled, float factor);
    static void SetQuadraticDrag(bool enabled, float coefficient);

    void Update(Scene& scene, float deltaTime) override;

private:
    void Integrate(Registry& registry, float dt);
    void ResolveCollisions(Scene& scene, Registry& registry, float dt);
    bool IsCollidable(const Registry& reg, Entity e);
    void ApplyPositionCorrection(struct TransformComponent& t1, struct TransformComponent& t2, float r1, float r2, bool static1, bool static2);
    void ApplySpherePlaneCorrection(struct TransformComponent& sphereTrans, float radius, const class Plane& plane);
};
