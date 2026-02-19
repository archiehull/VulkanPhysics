#pragma once
#include <cmath>

struct Vec3
{
	double x{ 0.0 }, y{ 0.0 }, z{ 0.0 };
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator*(const Vec3& v, double s) { return { v.x * s, v.y * s, v.z * s }; }

inline double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double LengthSq(const Vec3& v) { return Dot(v, v); }

inline double Length(const Vec3& v) { return std::sqrt(LengthSq(v)); }

struct InfiniteLine
{
	Vec3 point;     
	Vec3 direction; 
};

struct Line
{
	Vec3 a;
	Vec3 b; // line segment from a to b
};

class Collider
{
public:
	explicit Collider(const Vec3& position) : m_position(position) {}
	virtual ~Collider() = default;

	const Vec3& Position() const { return m_position; }
	void SetPosition(const Vec3& p) { m_position = p; }

	// Returns whether the point is inside the collider.
	virtual bool IsInside(const Vec3& point) const = 0;

	// Returns whether the line segment intersects the collider.
	virtual bool Intersects(const Line& line) const = 0;

protected:
	Vec3 m_position;
};