#include "pch.h"
#include "PhysicsHelper.h"
#include <algorithm>
#include <cmath>

MovingSphere::MovingSphere(const glm::vec3& pos, float r, const glm::vec3& vel, float invM, float rest)
	: sphere(pos, r), velocity(vel), forceAccumulator(0.0f), invMass(invM), restitution(rest), orientation(glm::mat3(1.0f)), angularVelocity(glm::vec3(0.0f))
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
	if (body.invMass <= 0.0f) return;
	body.velocity += impulse * body.invMass;
}

void ApplyForce(MovingSphere& body, const glm::vec3& force)
{
	if (body.invMass <= 0.0f) return;
	body.forceAccumulator += force;
}

float GetTotalSystemEnergy(const MovingSphere* bodies, int count)
{
	if (bodies == nullptr || count <= 0) return 0.0f;

	float totalEnergy = 0.0f;
	for (int i = 0; i < count; ++i) {
		totalEnergy += GetKineticEnergy(bodies[i]);
	}
	return totalEnergy;
}

glm::vec3 GetTotalSystemMomentum(const MovingSphere* bodies, int count)
{
	if (bodies == nullptr || count <= 0) return glm::vec3(0.0f);

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
	const float nLenSq = glm::dot(normal, normal);
	if (nLenSq <= 1e-12f) return 0.0f;

	const glm::vec3 n = normal / std::sqrt(nLenSq);
	const float vBefore = glm::dot(v1Before, n);
	const float vAfter = glm::dot(v1After, n);

	if (std::abs(vBefore) < 1e-6f) return 0.0f;

	const float e = -vAfter / vBefore;
	return glm::clamp(e, 0.0f, 1.0f);
}

void ApplyLinearDamping(MovingSphere& body, float damping, float dt)
{
	if (body.invMass <= 0.0f || dt <= 0.0f) return;

	// Interpret damping as [0,1], where 1 = no loss, 0 = full stop
	const float d = glm::clamp(damping, 0.0f, 1.0f);
	const float dampingFactor = std::pow(d, dt);
	body.velocity *= dampingFactor;
}

void ApplyQuadraticDrag(MovingSphere& body, float dragCoefficient, float dt)
{
	if (body.invMass <= 0.0f || dragCoefficient <= 0.0f || dt <= 0.0f) return;

	const float speed = glm::length(body.velocity);
	if (speed < 1e-6f) return;

	const glm::vec3 dragDir = -body.velocity / speed;
	const float dragMagnitude = dragCoefficient * speed * speed;

	// F * dt => impulse, then apply with invMass
	const glm::vec3 dragImpulse = dragDir * dragMagnitude * dt;
	ApplyImpulse(body, dragImpulse);
}

// Apply an angular displacement (rotation) around axis by angleRadians
void ApplyAngularDisplacement(MovingSphere& body, const glm::vec3& axis, float angleRadians)
{
	// Build a 4x4 rotation then extract the 3x3 portion
	glm::mat4 rot4 = glm::rotate(glm::mat4(1.0f), angleRadians, glm::normalize(axis));
	glm::mat3 rot3 = glm::mat3(rot4);

	// Apply rotation: newOrientation = rot * oldOrientation
	body.orientation = rot3 * body.orientation;
}

// Integrate angular velocity to update orientation
void IntegrateAngularVelocity(MovingSphere& body, float dt)
{
	if (dt <= 0.0f) return;

	const float speed = glm::length(body.angularVelocity);
	if (speed <= 1e-8f) return; // nothing to do

	const glm::vec3 axis = body.angularVelocity / speed;
	const float angle = speed * dt; // radians rotated this step
	ApplyAngularDisplacement(body, axis, angle);
}