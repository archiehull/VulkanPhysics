#include "pch.h"
#include "PhysicsHelper.h"
#include <algorithm>
#include <cmath>

MovingSphere::MovingSphere(const glm::vec3& pos, float r, const glm::vec3& vel, float invM, float rest)
	: sphere(pos, r), velocity(vel), forceAccumulator(0.0f), invMass(invM), restitution(rest), orientation(glm::mat3(1.0f)), angularVelocity(glm::vec3(0.0f)), torqueAccumulator(glm::vec3(0.0f)), inertiaTensor(glm::mat3(1.0f)), inverseInertiaTensor(glm::mat3(1.0f))
{
	// Compute inertia for a solid sphere if dynamic (invMass > 0)
	if (invMass > 0.0f) {
		float mass = 1.0f / invMass;
		float i = (2.0f / 5.0f) * mass * (r * r);
		inertiaTensor = glm::mat3(i);
		inverseInertiaTensor = glm::mat3(1.0f / i);
	}
	else {
		inertiaTensor = glm::mat3(0.0f);
		inverseInertiaTensor = glm::mat3(0.0f);
	}
}

MovingCapsule::MovingCapsule(const Capsule& cap, const glm::vec3& vel, float invM, float rest)
	: capsule(cap), velocity(vel), forceAccumulator(0.0f), invMass(invM), restitution(rest), orientation(glm::mat3(1.0f)), angularVelocity(glm::vec3(0.0f)), torqueAccumulator(glm::vec3(0.0f)), inertiaTensor(glm::mat3(1.0f)), inverseInertiaTensor(glm::mat3(1.0f))
{
	if (invMass > 0.0f) {
		float mass = 1.0f / invMass;
		glm::vec3 axis = cap.m_p2 - cap.Position();
		float h = glm::length(axis);
		float r = cap.m_radius;
		// Approximate inertia as a cylinder
		float ixx = (1.0f / 12.0f) * mass * (3.0f * r * r + h * h);
		float iyy = (0.5f) * mass * (r * r);
		// Local inertia tensor (assuming Y is along the axis, but here the axis is arbitrary)
		// For simplicity we use a spherical approximation or isotropic inertia if we don't know the local orientation,
		// but since we track orientation, let's just use an isotropic approximation for now, or use the bounding sphere.
		// A full tensor requires aligning it with the local axis. Let's use a spherical approximation based on average radius to avoid complex tensor rotation for now.
		float rAvg = (h / 2.0f + r);
		float i = (2.0f / 5.0f) * mass * (rAvg * rAvg);
		inertiaTensor = glm::mat3(i);
		inverseInertiaTensor = glm::mat3(1.0f / i);
	}
	else {
		inertiaTensor = glm::mat3(0.0f);
		inverseInertiaTensor = glm::mat3(0.0f);
	}
}

void ResolveElasticCollision(MovingSphere& a, MovingSphere& b, bool useForce, float dt, float friction)
{
	glm::vec3 delta = b.sphere.Position() - a.sphere.Position();
	float distSq = glm::dot(delta, delta);
	if (distSq == 0.0f) return;

	// Contact normal from A -> B
	glm::vec3 n = delta / std::sqrt(distSq);

	// Contact points relative to each center of mass
	glm::vec3 rA = n * a.sphere.m_radius;
	glm::vec3 rB = -n * b.sphere.m_radius;

	// Surface point velocity = linear + angular component
	glm::vec3 vAp = a.velocity + glm::cross(a.angularVelocity, rA);
	glm::vec3 vBp = b.velocity + glm::cross(b.angularVelocity, rB);
	glm::vec3 relVel = vAp - vBp;

	float velAlongNormal = glm::dot(relVel, n);
	if (velAlongNormal <= 0.0f) return;

	const float e = a.restitution * b.restitution;
	const float invMassSum = a.invMass + b.invMass;
	if (invMassSum <= 0.0f) return;

	// 1) Normal impulse
	glm::vec3 rA_cross_n = glm::cross(rA, n);
	glm::vec3 rB_cross_n = glm::cross(rB, n);
	float angularTermA = glm::dot(a.inverseInertiaTensor * rA_cross_n, rA_cross_n);
	float angularTermB = glm::dot(b.inverseInertiaTensor * rB_cross_n, rB_cross_n);

	float j = -(1.0f + e) * velAlongNormal / (invMassSum + angularTermA + angularTermB);
	glm::vec3 normalImpulse = n * j;

	// 2) Tangential (friction) impulse
	glm::vec3 tangent = relVel - (n * velAlongNormal);
	float tangentLen = glm::length(tangent);
	glm::vec3 frictionImpulse(0.0f);

	if (tangentLen > 1e-6f) {
		glm::vec3 t = tangent / tangentLen;

		glm::vec3 rA_cross_t = glm::cross(rA, t);
		glm::vec3 rB_cross_t = glm::cross(rB, t);

		float angularTermA = glm::dot(a.inverseInertiaTensor * rA_cross_t, rA_cross_t);
		float angularTermB = glm::dot(b.inverseInertiaTensor * rB_cross_t, rB_cross_t);

		float jt = -tangentLen / (invMassSum + angularTermA + angularTermB);
		const float maxFriction = std::abs(j) * glm::clamp(friction, 0.0f, 1.0f);
		jt = glm::clamp(jt, -maxFriction, maxFriction);

		frictionImpulse = t * jt;
	}

	// 3) Apply impulse (linear + angular)
	glm::vec3 totalImpulse = normalImpulse + frictionImpulse;

	if (useForce && dt > 0.0f) {
		glm::vec3 force = totalImpulse / dt;
		if (a.invMass > 0.0f) {
			a.forceAccumulator += force;
			a.torqueAccumulator += glm::cross(rA, force);
		}
		if (b.invMass > 0.0f) {
			b.forceAccumulator -= force;
			b.torqueAccumulator -= glm::cross(rB, force);
		}
	}
	else {
		if (a.invMass > 0.0f) {
			a.velocity += totalImpulse * a.invMass;
			a.angularVelocity += a.inverseInertiaTensor * glm::cross(rA, totalImpulse);
		}
		if (b.invMass > 0.0f) {
			b.velocity -= totalImpulse * b.invMass;
			b.angularVelocity -= b.inverseInertiaTensor * glm::cross(rB, totalImpulse);
		}
	}
}

float ComputeContactFriction(float frictionA, float frictionB, float globalScale)
{
	const float objectFriction = (frictionA + frictionB) * 0.5f;
	return glm::clamp(objectFriction * globalScale, 0.0f, 1.0f);
}

void ResolveSpherePlaneCollision(
	MovingSphere& a,
	const Plane& p,
	float planeRestitution,
	float contactFriction,
	const glm::vec3& planePointVelocity)
{
	if (a.invMass <= 0.0f) return;

	glm::vec3 n = p.GetNormal();
	glm::vec3 rA = -n * a.sphere.m_radius;

	// Contact point velocity relative to the wall point velocity
	glm::vec3 vAp = a.velocity + glm::cross(a.angularVelocity, rA);
	glm::vec3 relVel = vAp - planePointVelocity;
	float vn = glm::dot(relVel, n);
	if (vn >= 0.0f) return;

	float e = glm::clamp(a.restitution * planeRestitution, 0.0f, 1.0f);

	// 1) Normal impulse (accounting for angular inertia)
	glm::vec3 rCrossN = glm::cross(rA, n);
	glm::vec3 angularEffect = glm::cross(a.inverseInertiaTensor * rCrossN, rA);
	float invMassEffect = a.invMass + glm::dot(angularEffect, n);

	float j = -(1.0f + e) * vn;
	j /= invMassEffect;
	glm::vec3 normalImpulse = n * j;

	// Apply normal impulse
	a.velocity += normalImpulse * a.invMass;
	a.angularVelocity += a.inverseInertiaTensor * glm::cross(rA, normalImpulse);

	// 2) Tangential friction impulse (recalculate after normal impulse)
	vAp = a.velocity + glm::cross(a.angularVelocity, rA);
	relVel = vAp - planePointVelocity;
	glm::vec3 tangent = relVel - (n * glm::dot(relVel, n));
	float tangentLen = glm::length(tangent);
	glm::vec3 frictionImpulse(0.0f);

	if (tangentLen > 1e-6f) {
		glm::vec3 t = tangent / tangentLen;
		glm::vec3 rA_cross_t = glm::cross(rA, t);
		float angularTerm = glm::dot(a.inverseInertiaTensor * rA_cross_t, rA_cross_t);

		float jt = -glm::dot(relVel, t) / (a.invMass + angularTerm);
		const float maxFriction = std::abs(j) * glm::clamp(contactFriction, 0.0f, 1.0f);
		jt = glm::clamp(jt, -maxFriction, maxFriction);

		frictionImpulse = t * jt;
	}

	// 3) Apply frictional impulse
	a.velocity += frictionImpulse * a.invMass;
	a.angularVelocity += a.inverseInertiaTensor * glm::cross(rA, frictionImpulse);
}

void ResolveCapsulePlaneCollision(
	MovingCapsule& a,
	const Plane& p,
	float planeRestitution,
	float contactFriction)
{
	if (a.invMass <= 0.0f) return;

	glm::vec3 n = p.GetNormal();
	glm::vec3 com = (a.capsule.Position() + a.capsule.m_p2) * 0.5f;
	
	// Check both endpoints
	glm::vec3 points[2] = { a.capsule.Position(), a.capsule.m_p2 };
	for (int i = 0; i < 2; ++i) {
		float dist = p.GetSignedDistance(points[i]);
		if (dist <= a.capsule.m_radius) {
			// Collision at this point
			glm::vec3 contactPoint = points[i] - n * a.capsule.m_radius;
			glm::vec3 rA = contactPoint - com;
			
			glm::vec3 vAp = a.velocity + glm::cross(a.angularVelocity, rA);
			float vn = glm::dot(vAp, n);
			if (vn >= 0.0f) continue;
			
			float e = glm::clamp(a.restitution * planeRestitution, 0.0f, 1.0f);
			
			glm::vec3 rCrossN = glm::cross(rA, n);
			glm::vec3 angularEffect = glm::cross(a.inverseInertiaTensor * rCrossN, rA);
			float invMassEffect = a.invMass + glm::dot(angularEffect, n);
			
			float j = -(1.0f + e) * vn;
			j /= invMassEffect;
			// Halve impulse if both points are penetrating to avoid double energy
			// (A simplistic hack for multiple contacts, otherwise we need an LCP solver)
			bool bothPenetrating = (p.GetSignedDistance(points[0]) <= a.capsule.m_radius && 
									p.GetSignedDistance(points[1]) <= a.capsule.m_radius);
			if (bothPenetrating) j *= 0.5f;

			glm::vec3 normalImpulse = n * j;
			
			a.velocity += normalImpulse * a.invMass;
			a.angularVelocity += a.inverseInertiaTensor * glm::cross(rA, normalImpulse);
			
			// Friction
			vAp = a.velocity + glm::cross(a.angularVelocity, rA);
			glm::vec3 tangent = vAp - (n * glm::dot(vAp, n));
			float tangentLen = glm::length(tangent);
			
			if (tangentLen > 1e-6f) {
				glm::vec3 t = tangent / tangentLen;
				glm::vec3 rA_cross_t = glm::cross(rA, t);
				float angularTerm = glm::dot(a.inverseInertiaTensor * rA_cross_t, rA_cross_t);
				
				float jt = -glm::dot(vAp, t) / (a.invMass + angularTerm);
				float maxFriction = std::abs(j) * glm::clamp(contactFriction, 0.0f, 1.0f);
				jt = glm::clamp(jt, -maxFriction, maxFriction);
				
				glm::vec3 frictionImpulse = t * jt;
				a.velocity += frictionImpulse * a.invMass;
				a.angularVelocity += a.inverseInertiaTensor * glm::cross(rA, frictionImpulse);
			}
		}
	}
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

void ApplyImpulse(MovingCapsule& body, const glm::vec3& impulse)
{
	if (body.invMass <= 0.0f) return;
	body.velocity += impulse * body.invMass;
}

void ApplyForce(MovingSphere& body, const glm::vec3& force)
{
	if (body.invMass <= 0.0f) return;
	body.forceAccumulator += force;
}

void ApplyForce(MovingCapsule& body, const glm::vec3& force)
{
	if (body.invMass <= 0.0f) return;
	body.forceAccumulator += force;
}

// Apply force at a world-space point. Adds linear force and accumulates torque.
void ApplyForceAtPoint(MovingSphere& body, const glm::vec3& force, const glm::vec3& point) {
    if (body.invMass <= 0.0f) return;

    // Linear contribution: force acts on center of mass as well
    body.forceAccumulator += force;

    // r = point - centerOfMass
    glm::vec3 r = point - body.sphere.Position();

    // Torque = r x F
    glm::vec3 torque = glm::cross(r, force);
    body.torqueAccumulator += torque;
}

void ApplyForceAtPoint(MovingCapsule& body, const glm::vec3& force, const glm::vec3& point) {
    if (body.invMass <= 0.0f) return;

    body.forceAccumulator += force;
    glm::vec3 com = (body.capsule.Position() + body.capsule.m_p2) * 0.5f;
    glm::vec3 r = point - com;
    glm::vec3 torque = glm::cross(r, force);
    body.torqueAccumulator += torque;
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

void ApplyLinearDamping(MovingCapsule& body, float damping, float dt)
{
	if (body.invMass <= 0.0f || dt <= 0.0f) return;
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

void ApplyQuadraticDrag(MovingCapsule& body, float dragCoefficient, float dt)
{
	if (body.invMass <= 0.0f || dragCoefficient <= 0.0f || dt <= 0.0f) return;

	const float speed = glm::length(body.velocity);
	if (speed < 1e-6f) return;

	const glm::vec3 dragDir = -body.velocity / speed;
	const float dragMagnitude = dragCoefficient * speed * speed;

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

void ApplyAngularDisplacement(MovingCapsule& body, const glm::vec3& axis, float angleRadians)
{
	glm::mat4 rot4 = glm::rotate(glm::mat4(1.0f), angleRadians, glm::normalize(axis));
	glm::mat3 rot3 = glm::mat3(rot4);
	body.orientation = rot3 * body.orientation;
}

// Integrate angular velocity to update orientation
void IntegrateAngularVelocity(MovingSphere& body, float dt)
{
	if (dt <= 0.0f) return;

	// Convert accumulated torque into angular acceleration: alpha = I^-1 * tau
	if (glm::length(body.torqueAccumulator) > 0.0f) {
		// Multiply inverse inertia matrix by torque vector
		glm::vec3 angularAcceleration = body.inverseInertiaTensor * body.torqueAccumulator;
		body.angularVelocity += angularAcceleration * dt;
	}

	const float speed = glm::length(body.angularVelocity);
	if (speed <= 1e-8f) return; // nothing to do

	const glm::vec3 axis = body.angularVelocity / speed;
	const float angle = speed * dt; // radians rotated this step
	ApplyAngularDisplacement(body, axis, angle);

	// clear torque accumulator after integration
	body.torqueAccumulator = glm::vec3(0.0f);
}

void IntegrateAngularVelocity(MovingCapsule& body, float dt)
{
	if (dt <= 0.0f) return;

	if (glm::length(body.torqueAccumulator) > 0.0f) {
		glm::vec3 angularAcceleration = body.inverseInertiaTensor * body.torqueAccumulator;
		body.angularVelocity += angularAcceleration * dt;
	}

	const float speed = glm::length(body.angularVelocity);
	if (speed <= 1e-8f) return; 

	const glm::vec3 axis = body.angularVelocity / speed;
	const float angle = speed * dt;
	ApplyAngularDisplacement(body, axis, angle);

	body.torqueAccumulator = glm::vec3(0.0f);
}

void ResolveSphereInsideCylinder(MovingSphere& sphere, const glm::vec3& cylCenter, float cylRadius, float cylHeight, float restitution, float friction, const glm::vec3& cylAngularVelocity)
{
    glm::vec3 pos = sphere.sphere.Position();
    float r = sphere.sphere.m_radius;
    float halfHeight = cylHeight * 0.5f;

    // 1. Resolve Floor and Ceiling (Y-axis)
    if (pos.y - r < cylCenter.y - halfHeight) {
        // Use contact X,Z from sphere to compute proper contact point for wall velocity
        glm::vec3 contactPoint = glm::vec3(pos.x, cylCenter.y - halfHeight, pos.z);
        Plane floor(contactPoint, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 planePointVelocity = glm::cross(cylAngularVelocity, contactPoint - cylCenter);
        ResolveSpherePlaneCollision(sphere, floor, restitution, friction, planePointVelocity);
        pos = sphere.sphere.Position(); // Update pos after correction
    }
    else if (pos.y + r > cylCenter.y + halfHeight) {
        glm::vec3 contactPoint = glm::vec3(pos.x, cylCenter.y + halfHeight, pos.z);
        Plane ceiling(contactPoint, glm::vec3(0.0f, -1.0f, 0.0f));
        glm::vec3 planePointVelocity = glm::cross(cylAngularVelocity, contactPoint - cylCenter);
        ResolveSpherePlaneCollision(sphere, ceiling, restitution, friction, planePointVelocity);
        pos = sphere.sphere.Position();
    }

    // 2. Resolve the curved walls (XZ plane)
    glm::vec2 flatPos = glm::vec2(pos.x - cylCenter.x, pos.z - cylCenter.z);
    float dist = glm::length(flatPos);
    float maxAllowedDist = cylRadius - r;

    if (dist > maxAllowedDist && dist > 0.0001f) {
        glm::vec3 normal = glm::vec3(-flatPos.x / dist, 0.0f, -flatPos.y / dist); // Pointing inward
        glm::vec3 contactPoint = glm::vec3(cylCenter.x + (flatPos.x / dist) * cylRadius, pos.y, cylCenter.z + (flatPos.y / dist) * cylRadius);
        Plane wallPlane(contactPoint, normal);

        // include cylinder's angular surface velocity at contact:
        glm::vec3 planePointOffset = contactPoint - cylCenter;
        glm::vec3 planePointVelocity = glm::cross(cylAngularVelocity, planePointOffset);

        ResolveSpherePlaneCollision(sphere, wallPlane, restitution, friction, planePointVelocity);

        // Correct position
        glm::vec3 correctedPos = sphere.sphere.Position();
        glm::vec2 correctedFlat = glm::vec2(correctedPos.x - cylCenter.x, correctedPos.z - cylCenter.z);
        if (glm::length(correctedFlat) > maxAllowedDist) {
            glm::vec2 clampedFlat = glm::normalize(correctedFlat) * maxAllowedDist;
            sphere.sphere.SetPosition(glm::vec3(cylCenter.x + clampedFlat.x, correctedPos.y, cylCenter.z + clampedFlat.y));
        }
    }
}