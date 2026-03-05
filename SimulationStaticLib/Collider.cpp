#include "pch.h"
#include "Collider.h"

Collider::Collider(const glm::vec3& position) : m_position(position) {}

Collider::~Collider() = default;

const glm::vec3& Collider::Position() const { return m_position; }

void Collider::SetPosition(const glm::vec3& p) { m_position = p; }
