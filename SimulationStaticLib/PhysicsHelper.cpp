#include "pch.h"
#include "PhysicsHelper.h"

MovingSphere::MovingSphere(const glm::vec3& pos, float r, const glm::vec3& vel, float invM, float rest)
	: sphere(pos, r), velocity(vel), forceAccumulator(0.0f), invMass(invM), restitution(rest)
{
}

void ResolveElasticCollision(MovingSphere& a, MovingSphere& b, bool useForce, float dt)
{
	glm::vec3 normal = b.sphere.Position() - a.sphere.Position();
	float distSq = glm::dot(normal, normal);
	if (distSq == 0.0f) return;

	glm::vec3 relVel = a.velocity - b.velocity;
	float velAlongNormal = glm::dot(relVel, normal);
	if (velAlongNormal < 0.0f) return;

	double e = static_cast<double>(a.restitution) * static_cast<double>(b.restitution);
	double invMassSum = static_cast<double>(a.invMass) + static_cast<double>(b.invMass);
	if (invMassSum <= 0.0) return;

	double j = -((1.0 + e) * static_cast<double>(velAlongNormal));
	j /= (invMassSum * static_cast<double>(distSq));

	glm::vec3 impulse = normal * static_cast<float>(j);

	if (useForce && dt > 0.0f) {
		glm::vec3 force = impulse / dt;

		if (a.invMass > 0.0f) a.forceAccumulator += force;
		if (b.invMass > 0.0f) b.forceAccumulator -= force;
	}
	else {
		a.velocity += impulse * a.invMass;
		b.velocity -= impulse * b.invMass;
	}
}

void ResolveSpherePlaneCollision(
	MovingSphere& a,
	const Plane& p,
	float planeRestitution,
	float contactFriction)
{
	const glm::vec3 n = p.GetNormal();
	const float vn = glm::dot(a.velocity, n);

	if (vn >= 0.0f) return;

	const float e = glm::clamp(a.restitution * planeRestitution, 0.0f, 1.0f);

	a.velocity -= (1.0f + e) * vn * n;

	const float tangentDamping = glm::clamp(contactFriction, 0.0f, 1.0f);
	const glm::vec3 vNormal = glm::dot(a.velocity, n) * n;
	const glm::vec3 vTangent = a.velocity - vNormal;
	a.velocity = vNormal + (vTangent * tangentDamping);
}

float GetKineticEnergy(const MovingSphere& body)
{
	if (body.invMass <= 0.0f) return 0.0f;
	return 0.5f * (1.0f / body.invMass) * glm::dot(body.velocity, body.velocity);
}

glm::vec3 GetMomentum(const MovingSphere& body)
{
	if (body.invMass <= 0.0f) return glm::vec3(0.0f);
	return body.velocity * (1.0f / body.invMass);
}

// === NEW IMPLEMENTATIONS ===

void ApplyImpulse(MovingSphere& body, const glm::vec3& impulse)
{
    if (body.invMass > 0.0f) {
        body.velocity += impulse * body.invMass;
    }
}

void ApplyForce(MovingSphere& body, const glm::vec3& force)
{
    body.forceAccumulator += force;
}

float GetTotalSystemEnergy(const MovingSphere* bodies, int count)
{
    float totalEnergy = 0.0f;
    for (int i = 0; i < count; ++i) {
        totalEnergy += GetKineticEnergy(bodies[i]);
    }
    return totalEnergy;
}

glm::vec3 GetTotalSystemMomentum(const MovingSphere* bodies, int count)
{
    glm::vec3 totalMomentum(0.0f);
    for (int i = 0; i < count; ++i) {
        totalMomentum += GetMomentum(bodies[i]);
    }
    return totalMomentum;
}

glm::vec3 GetRelativeVelocity(const MovingSphere& a, const MovingSphere& b)
{
    return a.velocity - b.velocity;
}

float CalculateRestitutionFromVelocities(const glm::vec3& v1Before, const glm::vec3& v1After, const glm::vec3& normal)
{
    float vBefore = glm::dot(v1Before, normal);
    float vAfter = glm::dot(v1After, normal);
    
    if (std::abs(vBefore) < 1e-6f) return 0.0f;
    
    return std::abs(vAfter / vBefore);
}

void ApplyLinearDamping(MovingSphere& body, float damping, float dt)
{
    float dampingFactor = std::pow(damping, dt);
    body.velocity *= dampingFactor;
}

void ApplyQuadraticDrag(MovingSphere& body, float dragCoefficient, float dt)
{
    float speed = glm::length(body.velocity);
    if (speed < 1e-6f) return;
    
    float dragMagnitude = dragCoefficient * speed * speed;
    glm::vec3 dragForce = -glm::normalize(body.velocity) * dragMagnitude;
    
    body.forceAccumulator += dragForce;
}
