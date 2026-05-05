#pragma once
#include <unordered_set>
#include <vector>
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
    static bool applySleepNormalThreshold;
    static bool applySleepTangentialThreshold;

    // Drag / damping controls (opt-in)
    static bool applyLinearDamping;
    static float linearDampingFactor;      // [0..1], 1 = no damping
    static bool applyQuadraticDrag;
    static float quadraticDragCoefficient; // ~0.0f..1.0f depending scale

    static bool simulationPaused;

    static int localPeerId;
    static bool activePeers[4];

    // Setters for runtime adjustment
    static void SetLinearDamping(bool enabled, float factor);
    static void SetQuadraticDrag(bool enabled, float coefficient);
    static void SetSleepThresholds(bool normalEnabled, float normalThreshold, bool tangentialEnabled, float tangentialThreshold);

    void Update(Scene& scene, float deltaTime) override;
    bool IsPhysics() const override { return true; }

private:
    void Integrate(Registry& registry, float dt);
    void ResolveCollisions(Scene& scene, Registry& registry, float dt, const std::vector<Entity>& activeColliders, const std::vector<Entity>& activeSpheres, std::unordered_set<Entity>& pendingDelete);
    bool IsCollidable(const Registry& reg, Entity e);
    static void ApplyPositionCorrection(struct TransformComponent& t1, struct TransformComponent& t2, float r1, float r2, bool static1, bool static2);
    static void ApplySpherePlaneCorrection(struct TransformComponent& sphereTrans, float radius, const class Plane& plane);
};
