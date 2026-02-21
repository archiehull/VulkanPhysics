#pragma once

#include <glm/glm.hpp>
#include <string>
#include <memory>
#include "../geometry/Geometry.h"
#include "ECS.h"
#include "CoreTypes.h"
#include "../core/Config.h"

// 1. Identification
struct NameComponent {
    std::string name;
};

// 2. Spatial Data
struct TransformComponent {
    glm::mat4 matrix = glm::mat4(1.0f);
};

// 3. Visual Data
struct RenderComponent {
    std::shared_ptr<Geometry> geometry;
    std::string texturePath;
    std::string originalTexturePath;
    
    Entity simpleShadowEntity = MAX_ENTITIES;

    int shadingMode = 1;
    bool visible = true;
    bool castsShadow = true;
    bool receiveShadows = true;
    int layerMask = SceneLayers::INSIDE;
};

// 4. Movement/Logic Data
struct OrbitComponent {
    bool isOrbiting = false;
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 1.0f;
    float speed = 1.0f;
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 startVector = glm::vec3(1.0f, 0.0f, 0.0f);
    float initialAngle = 0.0f;
    float currentAngle = 0.0f;
};

// 5. Fire/Thermodynamics State
struct ThermoComponent {
    ObjectState state = ObjectState::NORMAL;
    bool isFlammable = false;

    float currentTemp = 20.0f;
    float ignitionThreshold = 100.0f;
    float thermalResponse = 5.0f;
    float selfHeatingRate = 15.0f;

    float burnTimer = 0.0f;
    float maxBurnDuration = 10.0f;
    float regrowTimer = 0.0f;
    float burnFactor = 0.0f;

    int fireEmitterId = -1;
    int smokeEmitterId = -1;
    int fireLightEntity = -1;

    std::shared_ptr<Geometry> storedOriginalGeometry = nullptr;
    glm::mat4 storedOriginalTransform = glm::mat4(1.0f);
};

// 6. Physics
struct ColliderComponent {
    bool hasCollision = true;
    float radius = 2.0f;
    float height = 5.0f;
};

// 7. Light
struct LightComponent {
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    int type = 0;
    int layerMask = SceneLayers::ALL;
};

// 8. Global Environment / Time / Weather Data
struct EnvironmentComponent {
    // Configurations
    TimeConfig timeConfig;
    SeasonConfig seasonConfig;
    WeatherConfig weatherConfig;
    float sunHeatBonus = 60.0f;

    // Time & Season State
    Season currentSeason = Season::SUMMER;
    float seasonTimer = 0.0f;

    // Weather State
    bool isPrecipitating = false;
    float weatherTimer = 0.0f;
    float currentWeatherDurationTarget = 10.0f;
    float weatherIntensity = 0.0f; // Stores the current global temperature

    // Fire & Dust interactions
    float postRainFireSuppressionTimer = 0.0f;
    float timeSinceLastRain = 0.0f;

    float currentSunHeight = 0.0f;
    bool useSimpleShadows = false;
};

struct DustCloudComponent {
    bool isActive = false;
    int emitterId = -1;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f);
    float speed = 15.0f;
};

// 10. Camera Data
struct CameraComponent {
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // Computed every frame by the CameraSystem
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);

    bool isActive = false;

    // Euler angles for Free Roam (can be moved to a separate Control component later)
    float yaw = -90.0f;
    float pitch = 0.0f;
};