#include "Camera.h"

Camera::Camera()
    : position(0.0f, 0.0f, 3.0f)
    , front(0.0f, 0.0f, -1.0f)
    , up(0.0f, 1.0f, 0.0f)
    , worldUp(0.0f, 1.0f, 0.0f)
    , yaw(-90.0f)
    , pitch(0.0f)
    , fov(60.0f)
    , nearPlane(1.0f)
    , farPlane(1000.0f)
    , moveSpeed(125.0f)
    , rotateSpeed(100.0f)
{
    UpdateCameraVectors();
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::GetProjectionMatrix(float aspectRatio) const {
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    proj[1][1] *= -1;  // Flip Y for Vulkan
    return proj;
}

void Camera::SetTarget(const glm::vec3& target) {
    front = glm::normalize(target - position);

    // Recalculate yaw and pitch from front vector
    yaw = glm::degrees(atan2(front.z, front.x));
    pitch = glm::degrees(asin(front.y));

    UpdateCameraVectors();
}

void Camera::SetUp(const glm::vec3& newUp) {
    worldUp = newUp;
    UpdateCameraVectors();
}

void Camera::MoveForward(float delta) {
    position += front * moveSpeed * delta;
}

void Camera::MoveBackward(float delta) {
    position -= front * moveSpeed * delta;
}

void Camera::MoveLeft(float delta) {
    position -= right * moveSpeed * delta;
}

void Camera::MoveRight(float delta) {
    position += right * moveSpeed * delta;
}

void Camera::MoveUp(float delta) {
    position += worldUp * moveSpeed * delta;
}

void Camera::MoveDown(float delta) {
    position -= worldUp * moveSpeed * delta;
}

void Camera::RotateYaw(float delta) {
    yaw += delta * rotateSpeed;
    UpdateCameraVectors();
}

void Camera::RotatePitch(float delta) {
    pitch += delta * rotateSpeed;

    // Constrain pitch
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;

    UpdateCameraVectors();
}

void Camera::UpdateCameraVectors() {
    // Calculate new front vector
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(newFront);

    // Recalculate right and up vectors
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}