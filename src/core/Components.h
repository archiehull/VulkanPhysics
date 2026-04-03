#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <memory>
#include <vector>
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
    std::string geometryName = "Unknown";
    std::string texturePath;
    std::string originalTexturePath;
    bool useDebugOverlay = false;
    glm::vec4 debugOverlayColor = glm::vec4(0.0f);

    Entity simpleShadowEntity = MAX_ENTITIES;
    float simpleShadowRadius = -1.0f;

    int shadingMode = 1;
    bool visible = true;
    bool castsShadow = true;
    bool originalCastsShadow = true;
    bool receiveShadows = true;
    int layerMask = SceneLayers::ALL_USED;
    int onlyInRegionMask = 0;
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
    glm::vec3 forceAccumulator = glm::vec3(0.0f); // Stores all forces for the current frame

    float mass = 1.0f;
    float inverseMass = 1.0f; // Always ensure this is 1.0f / mass!

    bool isStatic = true;
    float friction = 0.98f;
    float restitution = 1.0f;

    // Helper to safely set mass and update inverse mass
    void SetMass(float newMass) {
        if (newMass <= 0.0f) {
            mass = 0.0f;
            inverseMass = 0.0f; // Infinite mass (static object)
        }
        else {
            mass = newMass;
            inverseMass = 1.0f / mass;
        }
    }

    // Allow external systems to add forces to this component in a consistent way
    void ApplyForce(const glm::vec3& force) {
        if (!isStatic) {
            forceAccumulator += force;
        }
    }

    // --- ROTATIONAL STATE ---
    // The direction is the axis of rotation, magnitude is radians per second
    glm::vec3 angularVelocity = glm::vec3(0.0f);

    // Orientation stored as a 3x3 rotation matrix (column-major). Identity = no rotation
    glm::mat3 orientation = glm::mat3(1.0f);

    // --- NEW: Torque & Inertia ---
    // Accumulated torque for the current frame (world-space)
    glm::vec3 torqueAccumulator = glm::vec3(0.0f);

    // Inertia tensor (body-space) and inverse. For a sphere this can be set
    // using SetSphereInertia(). Stored as 3x3 matrices.
    glm::mat3 inertiaTensor = glm::mat3(1.0f);
    glm::mat3 inverseInertiaTensor = glm::mat3(1.0f);

    // Helper to initialize inertia for a solid sphere: I = (2/5) * m * r^2
    void SetSphereInertia(float radius) {
        if (isStatic || mass <= 0.0f) {
            inertiaTensor = glm::mat3(0.0f);
            inverseInertiaTensor = glm::mat3(0.0f);
        }
        else {
            float i = (2.0f / 5.0f) * mass * (radius * radius);
            inertiaTensor = glm::mat3(i);
            inverseInertiaTensor = glm::mat3(1.0f / i);
        }
    }
};

struct SpringComponent {
    std::vector<Entity> connectedEntities; // Multiple entities can be attached to this spring hub
    glm::vec3 fixedAnchorPoint = glm::vec3(0.0f); // Used if not attached to entities

    float restingLength = 1.0f; // Lr: The length at which the spring is at rest
    float stiffness = 10.0f;    // k: The spring constant (higher = stiffer)
    float damping = 1.0f;       // b: Damping coefficient to prevent infinite oscillation

    // Optional: flag to determine if it's attached to entities or anchored to a point in space
    bool isAttachedToEntity = true;
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
    int layerMask = SceneLayers::LAYER_A;

    bool flickerEnabled = false;
    float flickerAmount = 0.5f;
    int flickerPreset = 0; // 0=None, 1=Fire, 2=Candle, 3=Faulty, 4=Pulse
    float flickerPhase = 0.0f;

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

struct ObjectSpawnerComponent {
    bool alwaysOn = true;
    bool isRunning = true;

    float spawnInterval = 1.0f;
    float spawnTimer = 0.0f;

    float runDurationSeconds = -1.0f; // -1 = unlimited
    float runElapsedSeconds = 0.0f;
    int maxSpawnsPerRun = -1;         // -1 = unlimited
    int spawnedThisRun = 0;

    std::string spawnGeometryType = "Sphere"; // Sphere, Cube, Model
    std::string spawnModelPath = "";
    std::string spawnTexturePath = "textures/default.jpg";
    glm::vec3 spawnScale = glm::vec3(1.0f);

    glm::vec3 spawnVelocity = glm::vec3(0.0f, 10.0f, 0.0f);
    bool randomizeVelocity = false;
    glm::vec3 randomVelocityRange = glm::vec3(0.0f);

    // === NEW: Angular Velocity (radians/sec). Direction = axis, magnitude = speed ===
    glm::vec3 spawnAngularVelocity = glm::vec3(0.0f);
    bool randomizeAngularVelocity = false;
    glm::vec3 randomAngularVelocityRange = glm::vec3(0.0f);

    float spawnMass = 1.0f;
    int spawnedCount = 0;
};

struct SpawnedFromSpawnerComponent {
    Entity sourceSpawner = MAX_ENTITIES;
};

struct DespawnerComponent {
    bool enabled = true;
};

// 10. Camera Data
struct CameraComponent {
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float aspectRatio = 16.0f / 9.0f;

    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);

    int viewMask = SceneLayers::LAYER_A;  
    int insideRegionMask = 0;             
    bool isActive = false;

    float yaw = -90.0f;
    float pitch = 0.0f;
    float moveSpeed = 35.0f;
    float rotateSpeed = 60.0f;
    bool noclipEnabled = false;
};

struct LayerRegionComponent {
    int assignedLayerBit = 0; // 1 = Layer B, 2 = Layer C, etc. (0 is reserved for Base World)
    std::string layerName = "New Layer Region";

    int volumeType = 0; // 0 = Sphere, 1 = Box (AABB)
    float radius = 10.0f;
    glm::vec3 halfExtents = glm::vec3(5.0f, 5.0f, 5.0f);

    bool showRegionDebug = false;
    glm::vec4 regionDebugColor = glm::vec4(0.5f, 0.8f, 1.0f, 0.25f);
};