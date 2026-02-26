#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <memory>
#include "../geometry/Geometry.h"
#include "ECS.h"
#include "CoreTypes.h"
#include "../core/Config.h"
#include "../rendering/ParticleSystem.h"


// 1. Identification
struct NameComponent {
    std::string name;
};

// 2. Spatial Data
struct TransformComponent {
    glm::mat4 matrix = glm::mat4(1.0f);

    // Explicit Source of Truth for the UI
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Stored in Degrees
    glm::vec3 scale = glm::vec3(1.0f);

    // Call this whenever pos/rot/scale are changed
    void UpdateMatrix() {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        m = glm::scale(m, scale);
        matrix = m;
    }
};

// 3. Visual Data
struct RenderComponent {
    std::shared_ptr<Geometry> geometry;
    std::string texturePath;
    std::string originalTexturePath;
    
    Entity simpleShadowEntity = MAX_ENTITIES;
    float simpleShadowRadius = -1.0f;

    int shadingMode = 1;
    bool visible = true;
    bool castsShadow = true;
    bool originalCastsShadow = true;
    bool receiveShadows = true;
    int layerMask = SceneLayers::ALL;
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
    bool canBurnout = true;

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
    glm::vec3 storedOriginalPosition = glm::vec3(0.0f);
    glm::vec3 storedOriginalRotation = glm::vec3(0.0f);
    glm::vec3 storedOriginalScale = glm::vec3(1.0f);
};

// 6. Physics
struct PhysicsComponent {
    glm::vec3 velocity = glm::vec3(0.0f);
    float mass = 1.0f;
    bool isStatic = true; // If true, it won't move but others can bounce off it
    float friction = 0.98f; // Simple air/ground friction
	float restitution = 1.0f; // Elasticity: 1.0 = perfect bounce, 0.0 = no bounce
};

struct ColliderComponent {
    bool hasCollision = true;
    int type = 0; // 0 = Sphere, 1 = Plane
    float radius = 2.0f; // Used if type == 0
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f); // Used if type == 1
    float height = 5.0f;
};

// 7. Light
struct LightComponent {
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    int type = 0; // 0=Sun, 1=Fire, 2=Point, 3=Spotlight
    int layerMask = SceneLayers::ALL;

    // --- NEW Spotlight Variables ---
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f); // Default points straight down
    float cutoffAngle = 25.0f; // Cone width in degrees
};

struct ActiveEmitter {
    int emitterId = -1;
    float duration = -1.0f; // -1 means infinite
    float timer = 0.0f;
    float emissionRate = 100.0f;
    ParticleProps props; // We store a full copy of the properties here!
};

// A component that can hold MULTIPLE attached emitters
struct AttachedEmitterComponent {
    std::vector<ActiveEmitter> emitters;
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
    float aspectRatio = 16.0f / 9.0f;

    // Computed every frame by the CameraSystem
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);

    int viewMask = SceneLayers::ALL;
    bool isActive = false;

    // Euler angles for Free Roam
    float yaw = -90.0f;
    float pitch = 0.0f;

    float moveSpeed = 35.0f;
    float rotateSpeed = 60.0f;
};