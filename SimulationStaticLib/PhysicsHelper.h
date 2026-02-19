#pragma once
#include "Sphere.h"

struct MovingSphere {
    Sphere sphere;
    Vec3 velocity;
    double mass;

    MovingSphere(const Vec3& pos, float r, const Vec3& vel, double m = 1.0)
        : sphere(pos, r), velocity(vel), mass(m) {
    }
};

inline void ResolveElasticCollision(MovingSphere& a, MovingSphere& b) {
    Vec3 normal = b.sphere.Position() - a.sphere.Position();
    double distSq = LengthSq(normal);
    if (distSq == 0.0) return;

    double dist = std::sqrt(distSq);
    normal = normal * (1.0 / dist);

    Vec3 relVel = a.velocity - b.velocity;
    double velAlongNormal = Dot(relVel, normal);

    if (velAlongNormal < 0) return;

    double e = 1.0;
    double j = -(1.0 + e) * velAlongNormal;
    j /= (1.0 / a.mass + 1.0 / b.mass);

    Vec3 impulse = normal * j;
    a.velocity = a.velocity + (impulse * (1.0 / a.mass));
    b.velocity = b.velocity - (impulse * (1.0 / b.mass));
}

inline double GetKineticEnergy(const MovingSphere& body) {
    return 0.5 * body.mass * LengthSq(body.velocity);
}

inline Vec3 GetMomentum(const MovingSphere& body) {
    return body.velocity * body.mass;
}

inline void ExpectVec3Near(const Vec3& a, const Vec3& b, double tol = 1e-6) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}