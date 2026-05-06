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

void ResolveBoxBoxCollision(MovingBox& a, MovingBox& b) {
	glm::vec3 posA = a.box.Position();
	glm::vec3 posB = b.box.Position();
	glm::vec3 T = posB - posA;

	// Local axes (columns of the orientation matrix)
	glm::vec3 A[3] = { a.orientation[0], a.orientation[1], a.orientation[2] };
	glm::vec3 B[3] = { b.orientation[0], b.orientation[1], b.orientation[2] };
	glm::vec3 EA = a.box.m_halfExtents;
	glm::vec3 EB = b.box.m_halfExtents;

	// Compute the rotation matrix expressing B in A's coordinate frame
	glm::mat3 R, AbsR;
	const float EPSILON = 1e-6f;
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			R[i][j] = glm::dot(A[i], B[j]);
			AbsR[i][j] = std::abs(R[i][j]) + EPSILON; // Epsilon prevents division by zero with parallel axes
		}
	}

	float minPenetration = std::numeric_limits<float>::max();
	glm::vec3 hitNormal(0.0f);

	// Helper lambda to test an axis
	auto TestAxis = [&](const glm::vec3& axis, float rA, float rB, float dist) -> bool {
		float axisLengthSq = glm::dot(axis, axis);
		if (axisLengthSq < 1e-8f) return true; // Ignore parallel edge degenerate axes

		float p = rA + rB - std::abs(dist);
		if (p < 0.0f) return false; // Separating axis found! No collision.

		// Normalize penetration depth using the axis length
		p /= std::sqrt(axisLengthSq);
		if (p < minPenetration) {
			minPenetration = p;
			hitNormal = axis / std::sqrt(axisLengthSq);
			// Ensure normal always points from A to B
			if (glm::dot(hitNormal, T) < 0.0f) hitNormal = -hitNormal;
		}
		return true;
		};

	// 1. Test the 3 face normals of Box A
	float rA = EA.x;
	float rB = EB.x * AbsR[0][0] + EB.y * AbsR[0][1] + EB.z * AbsR[0][2];
	if (!TestAxis(A[0], rA, rB, glm::dot(T, A[0]))) return;

	rA = EA.y;
	rB = EB.x * AbsR[1][0] + EB.y * AbsR[1][1] + EB.z * AbsR[1][2];
	if (!TestAxis(A[1], rA, rB, glm::dot(T, A[1]))) return;

	rA = EA.z;
	rB = EB.x * AbsR[2][0] + EB.y * AbsR[2][1] + EB.z * AbsR[2][2];
	if (!TestAxis(A[2], rA, rB, glm::dot(T, A[2]))) return;

	// 2. Test the 3 face normals of Box B
	rA = EA.x * AbsR[0][0] + EA.y * AbsR[1][0] + EA.z * AbsR[2][0];
	rB = EB.x;
	if (!TestAxis(B[0], rA, rB, glm::dot(T, B[0]))) return;

	rA = EA.x * AbsR[0][1] + EA.y * AbsR[1][1] + EA.z * AbsR[2][1];
	rB = EB.y;
	if (!TestAxis(B[1], rA, rB, glm::dot(T, B[1]))) return;

	rA = EA.x * AbsR[0][2] + EA.y * AbsR[1][2] + EA.z * AbsR[2][2];
	rB = EB.z;
	if (!TestAxis(B[2], rA, rB, glm::dot(T, B[2]))) return;

	// 3. Test the 9 edge cross-products
	rA = EA.y * AbsR[2][0] + EA.z * AbsR[1][0];
	rB = EB.y * AbsR[0][2] + EB.z * AbsR[0][1];
	if (!TestAxis(glm::cross(A[0], B[0]), rA, rB, T.z * R[1][0] - T.y * R[2][0])) return;

	rA = EA.y * AbsR[2][1] + EA.z * AbsR[1][1];
	rB = EB.x * AbsR[0][2] + EB.z * AbsR[0][0];
	if (!TestAxis(glm::cross(A[0], B[1]), rA, rB, T.z * R[1][1] - T.y * R[2][1])) return;

	rA = EA.y * AbsR[2][2] + EA.z * AbsR[1][2];
	rB = EB.x * AbsR[0][1] + EB.y * AbsR[0][0];
	if (!TestAxis(glm::cross(A[0], B[2]), rA, rB, T.z * R[1][2] - T.y * R[2][2])) return;

	rA = EA.x * AbsR[2][0] + EA.z * AbsR[0][0];
	rB = EB.y * AbsR[1][2] + EB.z * AbsR[1][1];
	if (!TestAxis(glm::cross(A[1], B[0]), rA, rB, T.x * R[2][0] - T.z * R[0][0])) return;

	rA = EA.x * AbsR[2][1] + EA.z * AbsR[0][1];
	rB = EB.x * AbsR[1][2] + EB.z * AbsR[1][0];
	if (!TestAxis(glm::cross(A[1], B[1]), rA, rB, T.x * R[2][1] - T.z * R[0][1])) return;

	rA = EA.x * AbsR[2][2] + EA.z * AbsR[0][2];
	rB = EB.x * AbsR[1][1] + EB.y * AbsR[1][0];
	if (!TestAxis(glm::cross(A[1], B[2]), rA, rB, T.x * R[2][2] - T.z * R[0][2])) return;

	rA = EA.x * AbsR[1][0] + EA.y * AbsR[0][0];
	rB = EB.y * AbsR[2][2] + EB.z * AbsR[2][1];
	if (!TestAxis(glm::cross(A[2], B[0]), rA, rB, T.y * R[0][0] - T.x * R[1][0])) return;

	rA = EA.x * AbsR[1][1] + EA.y * AbsR[0][1];
	rB = EB.x * AbsR[2][2] + EB.z * AbsR[2][0];
	if (!TestAxis(glm::cross(A[2], B[1]), rA, rB, T.y * R[0][1] - T.x * R[1][1])) return;

	rA = EA.x * AbsR[1][2] + EA.y * AbsR[0][2];
	rB = EB.x * AbsR[2][1] + EB.y * AbsR[2][0];
	if (!TestAxis(glm::cross(A[2], B[2]), rA, rB, T.y * R[0][2] - T.x * R[1][2])) return;

	// === COLLISION DETECTED ===

	// Find the Support Point (approximate contact point)
	glm::vec3 contactPoint = posB;
	for (int i = 0; i < 3; ++i) {
		float sign = (glm::dot(B[i], -hitNormal) > 0.0f) ? 1.0f : -1.0f;
		contactPoint += B[i] * EB[i] * sign;
	}

	glm::vec3 rA_vec = contactPoint - posA;
	glm::vec3 rB_vec = contactPoint - posB;

	glm::vec3 vAc = a.velocity + glm::cross(a.angularVelocity, rA_vec);
	glm::vec3 vBc = b.velocity + glm::cross(b.angularVelocity, rB_vec);
	glm::vec3 relativeVelocity = vBc - vAc;

	float velAlongNormal = glm::dot(relativeVelocity, hitNormal);
	if (velAlongNormal >= 0.0f) return;

	float totalInvMass = a.invMass + b.invMass;
	if (totalInvMass <= 0.0f) return;

	// FIX: Convert Inverse Inertia Tensors to World Space
	glm::mat3 invInertiaWorldA = a.orientation * a.inverseInertiaTensor * glm::transpose(a.orientation);
	glm::mat3 invInertiaWorldB = b.orientation * b.inverseInertiaTensor * glm::transpose(b.orientation);

	// Compute Impulse
	glm::vec3 rACrossN = glm::cross(rA_vec, hitNormal);
	glm::vec3 rBCrossN = glm::cross(rB_vec, hitNormal);

	// FIX: Use World Space Inertia
	float angDenomA = glm::dot(hitNormal, glm::cross(invInertiaWorldA * rACrossN, rA_vec));
	float angDenomB = glm::dot(hitNormal, glm::cross(invInertiaWorldB * rBCrossN, rB_vec));
	float denom = std::max(totalInvMass + angDenomA + angDenomB, 1e-6f);

	float e = std::min(a.restitution, b.restitution);
	float j = -(1.0f + e) * velAlongNormal / denom;

	glm::vec3 impulse = j * hitNormal;

	a.velocity -= a.invMass * impulse;
	b.velocity += b.invMass * impulse;

	// FIX: Use World Space Inertia for application
	a.angularVelocity -= invInertiaWorldA * glm::cross(rA_vec, impulse);
	b.angularVelocity += invInertiaWorldB * glm::cross(rB_vec, impulse);

	// Compute Friction Impulse
	glm::vec3 tangent = relativeVelocity - (hitNormal * velAlongNormal);
	float tangentLen = glm::length(tangent);
	if (tangentLen > 1e-6f) {
		glm::vec3 t = tangent / tangentLen;
		glm::vec3 rACrossT = glm::cross(rA_vec, t);
		glm::vec3 rBCrossT = glm::cross(rB_vec, t);

		// FIX: Use World Space Inertia
		float tangDenomA = glm::dot(t, glm::cross(invInertiaWorldA * rACrossT, rA_vec));
		float tangDenomB = glm::dot(t, glm::cross(invInertiaWorldB * rBCrossT, rB_vec));
		float tDenom = std::max(totalInvMass + tangDenomA + tangDenomB, 1e-6f);

		float jt = -tangentLen / tDenom;
		float friction = ComputeContactFriction(0.5f, 0.5f, 1.0f);
		float maxFriction = std::abs(j) * friction;
		jt = glm::clamp(jt, -maxFriction, maxFriction);

		glm::vec3 frictionImpulse = jt * t;
		a.velocity -= a.invMass * frictionImpulse;
		b.velocity += b.invMass * frictionImpulse;

		// FIX: Use World Space Inertia
		a.angularVelocity -= invInertiaWorldA * glm::cross(rA_vec, frictionImpulse);
		b.angularVelocity += invInertiaWorldB * glm::cross(rB_vec, frictionImpulse);
	}

	// Positional correction
	const float percent = 0.2f;
	const float slop = 0.01f;
	float correctionMag = std::max(minPenetration - slop, 0.0f) / totalInvMass * percent;
	glm::vec3 correction = correctionMag * hitNormal;

	a.box.SetPosition(posA - a.invMass * correction);
	b.box.SetPosition(posB + b.invMass * correction);
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

void ClosestPtSegmentSegment(
	const glm::vec3& p1, const glm::vec3& q1,
	const glm::vec3& p2, const glm::vec3& q2,
	glm::vec3& c1, glm::vec3& c2)
{
	glm::vec3 d1 = q1 - p1;
	glm::vec3 d2 = q2 - p2;
	glm::vec3 r = p1 - p2;
	float a = glm::dot(d1, d1);
	float e = glm::dot(d2, d2);
	float f = glm::dot(d2, r);

	const float EPSILON = 1e-6f;
	float s = 0.0f, t = 0.0f;

	if (a <= EPSILON && e <= EPSILON) {
		s = t = 0.0f;
		c1 = p1; c2 = p2;
		return;
	}
	if (a <= EPSILON) {
		s = 0.0f;
		t = glm::clamp(f / e, 0.0f, 1.0f);
	}
	else {
		float c = glm::dot(d1, r);
		if (e <= EPSILON) {
			t = 0.0f;
			s = glm::clamp(-c / a, 0.0f, 1.0f);
		}
		else {
			float b = glm::dot(d1, d2);
			float denom = a * e - b * b;
			if (denom != 0.0f) {
				s = glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
			}
			else {
				s = 0.0f;
			}
			t = (b * s + f) / e;
			if (t < 0.0f) {
				t = 0.0f;
				s = glm::clamp(-c / a, 0.0f, 1.0f);
			}
			else if (t > 1.0f) {
				t = 1.0f;
				s = glm::clamp((b - c) / a, 0.0f, 1.0f);
			}
		}
	}
	c1 = p1 + d1 * s;
	c2 = p2 + d2 * t;
}

bool ResolveCapsuleCapsuleCollision(MovingCapsule& a, MovingCapsule& b) {
	// 1. The Capsule primitive stores the exact segment endpoints
	glm::vec3 p1A = a.capsule.Position();
	glm::vec3 p2A = a.capsule.m_p2;

	glm::vec3 p1B = b.capsule.Position();
	glm::vec3 p2B = b.capsule.m_p2;

	// 2. Find closest points on both segments
	glm::vec3 cA, cB;
	ClosestPtSegmentSegment(p1A, p2A, p1B, p2B, cA, cB);

	// 3. Check for collision
	glm::vec3 delta = cB - cA;
	float distSq = glm::dot(delta, delta);
	float rSum = a.capsule.m_radius + b.capsule.m_radius;

	if (distSq > rSum * rSum || distSq < 1e-8f) return false;

	float dist = std::sqrt(distSq);
	glm::vec3 hitNormal = delta / dist;
	float penetration = rSum - dist;

	// 4. Calculate contact points and velocities
	glm::vec3 contactPoint = cA + hitNormal * a.capsule.m_radius;
	glm::vec3 comA = (p1A + p2A) * 0.5f;
	glm::vec3 comB = (p1B + p2B) * 0.5f;
	glm::vec3 rA_vec = contactPoint - comA;
	glm::vec3 rB_vec = contactPoint - comB;

	glm::vec3 vAc = a.velocity + glm::cross(a.angularVelocity, rA_vec);
	glm::vec3 vBc = b.velocity + glm::cross(b.angularVelocity, rB_vec);
	glm::vec3 relativeVelocity = vBc - vAc;

	float velAlongNormal = glm::dot(relativeVelocity, hitNormal);
	if (velAlongNormal >= 0.0f) return false;

	// 5. Apply Impulse
	float totalInvMass = a.invMass + b.invMass;
	if (totalInvMass <= 0.0f) return false;

	glm::mat3 invInertiaWorldA = a.orientation * a.inverseInertiaTensor * glm::transpose(a.orientation);
	glm::mat3 invInertiaWorldB = b.orientation * b.inverseInertiaTensor * glm::transpose(b.orientation);

	glm::vec3 rACrossN = glm::cross(rA_vec, hitNormal);
	glm::vec3 rBCrossN = glm::cross(rB_vec, hitNormal);

	float angDenomA = glm::dot(hitNormal, glm::cross(invInertiaWorldA * rACrossN, rA_vec));
	float angDenomB = glm::dot(hitNormal, glm::cross(invInertiaWorldB * rBCrossN, rB_vec));
	float denom = std::max(totalInvMass + angDenomA + angDenomB, 1e-6f);

	float e = std::min(a.restitution, b.restitution);
	float j = -(1.0f + e) * velAlongNormal / denom;

	glm::vec3 impulse = j * hitNormal;
	a.velocity -= a.invMass * impulse;
	b.velocity += b.invMass * impulse;
	a.angularVelocity -= invInertiaWorldA * glm::cross(rA_vec, impulse);
	b.angularVelocity += invInertiaWorldB * glm::cross(rB_vec, impulse);

	// Friction
	glm::vec3 tangent = relativeVelocity - (hitNormal * velAlongNormal);
	float tangentLen = glm::length(tangent);
	if (tangentLen > 1e-6f) {
		glm::vec3 t = tangent / tangentLen;
		glm::vec3 rACrossT = glm::cross(rA_vec, t);
		glm::vec3 rBCrossT = glm::cross(rB_vec, t);
		float tDenomA = glm::dot(t, glm::cross(invInertiaWorldA * rACrossT, rA_vec));
		float tDenomB = glm::dot(t, glm::cross(invInertiaWorldB * rBCrossT, rB_vec));
		float tDenom = std::max(totalInvMass + tDenomA + tDenomB, 1e-6f);

		float jt = -tangentLen / tDenom;
		float maxFriction = std::abs(j) * 0.5f;
		jt = glm::clamp(jt, -maxFriction, maxFriction);

		glm::vec3 frictionImpulse = jt * t;
		a.velocity -= a.invMass * frictionImpulse;
		b.velocity += b.invMass * frictionImpulse;
		a.angularVelocity -= invInertiaWorldA * glm::cross(rA_vec, frictionImpulse);
		b.angularVelocity += invInertiaWorldB * glm::cross(rB_vec, frictionImpulse);
	}

	// 6. Positional Correction
	const float percent = 0.2f;
	const float slop = 0.01f;
	float correctionMag = std::max(penetration - slop, 0.0f) / totalInvMass * percent;
	glm::vec3 correction = correctionMag * hitNormal;

	glm::vec3 corrA = -a.invMass * correction;
	glm::vec3 corrB = b.invMass * correction;
	a.capsule.SetPosition(a.capsule.Position() + corrA);
	a.capsule.m_p2 += corrA;
	b.capsule.SetPosition(b.capsule.Position() + corrB);
	b.capsule.m_p2 += corrB;

	return true;
}

bool ResolveBoxCapsuleCollision(MovingBox& box, MovingCapsule& cap) {
	// 1. Get capsule segment in WORLD space directly from the primitive
	glm::vec3 p1World = cap.capsule.Position();
	glm::vec3 p2World = cap.capsule.m_p2;

	// 2. Convert segment to BOX LOCAL space
	glm::mat3 invBoxRot = glm::transpose(box.orientation);
	glm::vec3 boxCenter = box.box.Position();
	glm::vec3 p1Local = invBoxRot * (p1World - boxCenter);
	glm::vec3 p2Local = invBoxRot * (p2World - boxCenter);

	// 3. Find closest point between segment and box AABB in local space
	glm::vec3 samplePoints[3] = { p1Local, p2Local, (p1Local + p2Local) * 0.5f };

	float minPenetration = std::numeric_limits<float>::max();
	glm::vec3 bestLocalContact(0.0f);
	glm::vec3 bestLocalNormal(0.0f);
	bool hit = false;

	glm::vec3 extents = box.box.m_halfExtents;

	for (int i = 0; i < 3; ++i) {
		glm::vec3 pt = samplePoints[i];
		glm::vec3 closestPt = glm::clamp(pt, -extents, extents);
		glm::vec3 delta = pt - closestPt;

		float distSq = glm::dot(delta, delta);
		if (distSq < cap.capsule.m_radius * cap.capsule.m_radius) {
			float dist = std::sqrt(distSq);
			float penetration = cap.capsule.m_radius - dist;

			if (penetration < minPenetration) {
				minPenetration = penetration;
				bestLocalContact = closestPt;
				if (distSq < 1e-6f) {
					glm::vec3 absPt = glm::abs(pt);
					glm::vec3 distToFace = extents - absPt;
					if (distToFace.x < distToFace.y && distToFace.x < distToFace.z) {
						bestLocalNormal = glm::vec3(pt.x > 0 ? 1 : -1, 0, 0);
					}
					else if (distToFace.y < distToFace.z) {
						bestLocalNormal = glm::vec3(0, pt.y > 0 ? 1 : -1, 0);
					}
					else {
						bestLocalNormal = glm::vec3(0, 0, pt.z > 0 ? 1 : -1);
					}
				}
				else {
					bestLocalNormal = delta / dist;
				}
				hit = true;
			}
		}
	}

	if (!hit) return false;

	// 4. Convert back to world space
	glm::vec3 hitNormal = box.orientation * bestLocalNormal; // Points from Box -> Capsule
	glm::vec3 contactPoint = boxCenter + (box.orientation * bestLocalContact);

	// 5. Apply standard impulse resolution
	glm::vec3 comCap = (p1World + p2World) * 0.5f;
	glm::vec3 rBox = contactPoint - boxCenter;
	glm::vec3 rCap = contactPoint - comCap;

	glm::vec3 vBoxC = box.velocity + glm::cross(box.angularVelocity, rBox);
	glm::vec3 vCapC = cap.velocity + glm::cross(cap.angularVelocity, rCap);
	glm::vec3 relativeVelocity = vCapC - vBoxC;

	float velAlongNormal = glm::dot(relativeVelocity, hitNormal);
	if (velAlongNormal >= 0.0f) return false;

	float totalInvMass = box.invMass + cap.invMass;
	if (totalInvMass <= 0.0f) return false;

	glm::mat3 invInertiaWorldBox = box.orientation * box.inverseInertiaTensor * glm::transpose(box.orientation);
	glm::mat3 invInertiaWorldCap = cap.orientation * cap.inverseInertiaTensor * glm::transpose(cap.orientation);

	float angDenomBox = glm::dot(hitNormal, glm::cross(invInertiaWorldBox * glm::cross(rBox, hitNormal), rBox));
	float angDenomCap = glm::dot(hitNormal, glm::cross(invInertiaWorldCap * glm::cross(rCap, hitNormal), rCap));
	float denom = std::max(totalInvMass + angDenomBox + angDenomCap, 1e-6f);

	float e = std::min(box.restitution, cap.restitution);
	float j = -(1.0f + e) * velAlongNormal / denom;

	glm::vec3 impulse = j * hitNormal;
	box.velocity -= box.invMass * impulse;
	cap.velocity += cap.invMass * impulse;
	box.angularVelocity -= invInertiaWorldBox * glm::cross(rBox, impulse);
	cap.angularVelocity += invInertiaWorldCap * glm::cross(rCap, impulse);

	// Friction
	glm::vec3 tangent = relativeVelocity - (hitNormal * velAlongNormal);
	float tangentLen = glm::length(tangent);
	if (tangentLen > 1e-6f) {
		glm::vec3 t = tangent / tangentLen;
		glm::vec3 rBoxCrossT = glm::cross(rBox, t);
		glm::vec3 rCapCrossT = glm::cross(rCap, t);
		float tangDenomBox = glm::dot(t, glm::cross(invInertiaWorldBox * rBoxCrossT, rBox));
		float tangDenomCap = glm::dot(t, glm::cross(invInertiaWorldCap * rCapCrossT, rCap));
		float tDenom = std::max(totalInvMass + tangDenomBox + tangDenomCap, 1e-6f);

		float jt = -tangentLen / tDenom;
		float maxFriction = std::abs(j) * 0.5f;
		jt = glm::clamp(jt, -maxFriction, maxFriction);

		glm::vec3 frictionImpulse = jt * t;
		box.velocity -= box.invMass * frictionImpulse;
		cap.velocity += cap.invMass * frictionImpulse;
		box.angularVelocity -= invInertiaWorldBox * glm::cross(rBox, frictionImpulse);
		cap.angularVelocity += invInertiaWorldCap * glm::cross(rCap, frictionImpulse);
	}

	// Positional correction
	float correctionMag = std::max(minPenetration - 0.01f, 0.0f) / totalInvMass * 0.2f;
	glm::vec3 correction = correctionMag * hitNormal;

	box.box.SetPosition(boxCenter - box.invMass * correction);
	glm::vec3 corrCap = cap.invMass * correction;
	cap.capsule.SetPosition(cap.capsule.Position() + corrCap);
	cap.capsule.m_p2 += corrCap;

	return true;
}

bool ResolveSphereCapsuleCollision(MovingSphere& sphere, MovingCapsule& cap) {
	glm::vec3 p1 = cap.capsule.Position();
	glm::vec3 p2 = cap.capsule.m_p2;
	glm::vec3 sCenter = sphere.sphere.Position();

	glm::vec3 ab = p2 - p1;
	float denom = glm::dot(ab, ab);
	float t = 0.0f;
	if (denom > 1e-6f) {
		t = glm::dot(sCenter - p1, ab) / denom;
		t = glm::clamp(t, 0.0f, 1.0f);
	}
	glm::vec3 closestPt = p1 + t * ab;

	glm::vec3 delta = sCenter - closestPt;
	float distSq = glm::dot(delta, delta);
	float rSum = sphere.sphere.m_radius + cap.capsule.m_radius;

	// FIX: return false instead of return
	if (distSq > rSum * rSum || distSq < 1e-8f) return false;

	float dist = std::sqrt(distSq);
	glm::vec3 hitNormal = delta / dist;
	float penetration = rSum - dist;

	glm::vec3 contactPoint = closestPt + hitNormal * cap.capsule.m_radius;
	glm::vec3 rS = contactPoint - sCenter;
	glm::vec3 comCap = (p1 + p2) * 0.5f;
	glm::vec3 rC = contactPoint - comCap;

	glm::vec3 vSc = sphere.velocity + glm::cross(sphere.angularVelocity, rS);
	glm::vec3 vCc = cap.velocity + glm::cross(cap.angularVelocity, rC);
	glm::vec3 relativeVelocity = vSc - vCc;

	float velAlongNormal = glm::dot(relativeVelocity, hitNormal);
	// FIX: return false instead of return
	if (velAlongNormal >= 0.0f) return false;

	float totalInvMass = sphere.invMass + cap.invMass;
	// FIX: return false instead of return
	if (totalInvMass <= 0.0f) return false;

	glm::mat3 invInertiaWorldS = sphere.orientation * sphere.inverseInertiaTensor * glm::transpose(sphere.orientation);
	glm::mat3 invInertiaWorldC = cap.orientation * cap.inverseInertiaTensor * glm::transpose(cap.orientation);

	glm::vec3 rSCrossN = glm::cross(rS, hitNormal);
	glm::vec3 rCCrossN = glm::cross(rC, hitNormal);

	float angDenomS = glm::dot(hitNormal, glm::cross(invInertiaWorldS * rSCrossN, rS));
	float angDenomC = glm::dot(hitNormal, glm::cross(invInertiaWorldC * rCCrossN, rC));
	float resDenom = std::max(totalInvMass + angDenomS + angDenomC, 1e-6f);

	float e = std::min(sphere.restitution, cap.restitution);
	float j = -(1.0f + e) * velAlongNormal / resDenom;

	glm::vec3 impulse = j * hitNormal;
	sphere.velocity += sphere.invMass * impulse;
	cap.velocity -= cap.invMass * impulse;
	sphere.angularVelocity += invInertiaWorldS * glm::cross(rS, impulse);
	cap.angularVelocity -= invInertiaWorldC * glm::cross(rC, impulse);

	glm::vec3 tangent = relativeVelocity - (hitNormal * velAlongNormal);
	float tangentLen = glm::length(tangent);
	if (tangentLen > 1e-6f) {
		glm::vec3 t_dir = tangent / tangentLen;
		glm::vec3 rSCrossT = glm::cross(rS, t_dir);
		glm::vec3 rCCrossT = glm::cross(rC, t_dir);
		float tDenomS = glm::dot(t_dir, glm::cross(invInertiaWorldS * rSCrossT, rS));
		float tDenomC = glm::dot(t_dir, glm::cross(invInertiaWorldC * rCCrossT, rC));
		float tDenom = std::max(totalInvMass + tDenomS + tDenomC, 1e-6f);

		float jt = -tangentLen / tDenom;
		float maxFriction = std::abs(j) * 0.5f;
		jt = glm::clamp(jt, -maxFriction, maxFriction);

		glm::vec3 frictionImpulse = jt * t_dir;
		sphere.velocity += sphere.invMass * frictionImpulse;
		cap.velocity -= cap.invMass * frictionImpulse;
		sphere.angularVelocity += invInertiaWorldS * glm::cross(rS, frictionImpulse);
		cap.angularVelocity -= invInertiaWorldC * glm::cross(rC, frictionImpulse);
	}

	const float percent = 0.2f;
	const float slop = 0.01f;
	float correctionMag = std::max(penetration - slop, 0.0f) / totalInvMass * percent;
	glm::vec3 correction = correctionMag * hitNormal;

	sphere.sphere.SetPosition(sCenter + sphere.invMass * correction);
	glm::vec3 corrC = -cap.invMass * correction;
	cap.capsule.SetPosition(p1 + corrC);
	cap.capsule.m_p2 = p2 + corrC;

	// FIX: Return true to confirm collision for the despawner
	return true;
}