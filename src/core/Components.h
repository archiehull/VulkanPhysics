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
    float opacity = 1.0f;
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

enum class PathAnimationPlayMode { Once, Loop, Bounce };
enum class PathAnimationTimingMode { Absolute, PerSegment, OverallTime };
enum class PathAnimationEasing { Linear, Smoothstep };
enum class PathCurveType { Straight, BezierQuadratic };

struct PathWaypoint {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 orientation = glm::vec3(0.0f);
    float timeFromStart = 0.0f;
};

struct PathCurveSegment {
    PathCurveType curveType = PathCurveType::Straight;
    glm::vec3 controlPoint = glm::vec3(0.0f);
    float duration = 1.0f;
};

struct PathAnimationComponent {
    std::vector<PathWaypoint> waypoints;
    std::vector<PathCurveSegment> segments;
    PathAnimationPlayMode playMode = PathAnimationPlayMode::Once;
    PathAnimationTimingMode timingMode = PathAnimationTimingMode::Absolute;
    PathAnimationEasing easing = PathAnimationEasing::Linear;
    bool applyEasing = true;
    bool perPointRotation = false;
    glm::vec3 baseRotation = glm::vec3(0.0f);
    bool hasBaseRotation = false;
    float totalDuration = 5.0f;
    float playbackSpeed = 1.0f;
    bool isPlaying = true;
    bool showPath = false;
    glm::vec4 pathColor = glm::vec4(0.6f, 0.2f, 0.8f, 1.0f);
    bool reversePath = false;
    bool connectEndToStart = false;
    glm::vec3 animationVelocity = glm::vec3(0.0f);
    bool initialized = false;
    bool useLocalSpace = false;
    bool rotateAlongPath = false;
    glm::vec3 rotationOffset = glm::vec3(0.0f);
    bool applyConstantRotation = false;
    glm::vec3 rotationSpinRate = glm::vec3(0.0f);
    float currentTime = 0.0f;
    float rotationSpinTime = 0.0f;
    int playbackDirection = 1;
    bool lastReversePath = false;
    glm::vec3 lastEvaluatedPosition = glm::vec3(0.0f);
    bool hasLastEvaluatedPosition = false;
    glm::vec3 localOriginPosition = glm::vec3(0.0f);
    glm::vec3 localOriginRotation = glm::vec3(0.0f);
    bool hasLocalOrigin = false;
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
    std::vector<float> restingLengths;     // Optional: specific resting length for each connection
    glm::vec3 fixedAnchorPoint = glm::vec3(0.0f); // Used if not attached to entities

    float restingLength = 1.0f; // Lr: The length at which the spring is at rest
    float stiffness = 10.0f;    // k: The spring constant (higher = stiffer)
    float damping = 1.0f;       // b: Damping coefficient to prevent infinite oscillation

    // Optional: flag to determine if it's attached to entities or anchored to a point in space
    bool isAttachedToEntity = true;
};

struct ClothComponent {
    int width = 0;
    int height = 0;
    float spacing = 1.0f;
    std::vector<Entity> particles; // 1D array of entities in the grid
    std::shared_ptr<Geometry> dynamicGeometry = nullptr;
    bool collisionsEnabled = true;
    bool visualizeCollisionPolys = false;
};

enum class CollisionSide {
    OUTSIDE,
    INSIDE,
    BOTH 
};

struct ColliderComponent {
    bool hasCollision = true;
    bool isClothParticle = false;
    bool autoScale = true;
    int type = 0; // 0 = Sphere, 1 = Plane, 2 = Capsule, 3 = Box (AABB), 4 = Cube, 5 = Cylinder
    float radius = 2.0f; 
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f); 
    float height = 5.0f;
    glm::vec3 halfExtents = glm::vec3(0.0f);
    CollisionSide collisionSide = CollisionSide::OUTSIDE;
    float wallThickness = 0.1f;
    uint32_t collisionLayer = 1; // Default layer
    uint32_t collisionMask = 0xFFFFFFFF; // Collide with all layers by default
};

struct SpringVisualComponent {};

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

// Smoke Grenade logic state
struct SmokeGrenadeComponent {
    float delayBeforeSmoke = 2.0f;
    float smokeDuration = 10.0f;
    float timer = 0.0f;
    bool isEmitting = false;
    int smokeEmitterId = -1; // Keep track of the attached emitter
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
    bool triggerOnStartup = false;
    char group = 'A';

    float spawnInterval = 1.0f;
    float spawnTimer = 0.0f;

    float runDurationSeconds = -1.0f; // -1 = unlimited
    float runElapsedSeconds = 0.0f;
    int maxSpawnsPerRun = -1;         // -1 = unlimited
    int spawnedThisRun = 0;
    
    // Ownership tracking (0=Player1, 1=Player2, 2=Player3, 3=Player4)
    static constexpr uint8_t OWNER_SEQUENTIAL = 255;
    static constexpr uint8_t OWNER_LOCAL = 254;

    uint8_t assignedOwner = OWNER_LOCAL;      // Default to the person who spawned it
    uint8_t nextSequentialOwner = 0; // For SEQUENTIAL: tracks next player to own

    std::string spawnGeometryType = "Sphere"; // Sphere, Cube, Model
    std::string spawnModelPath = "";
    std::string spawnTexturePath = "textures/default.jpg";
    float spawnObjectScale = 1.0f; // Uniform scale used for spheres
    glm::vec3 spawnScale = glm::vec3(1.0f);

    glm::vec3 spawnVelocity = glm::vec3(0.0f, 10.0f, 0.0f);
    bool randomizeVelocity = false;
    glm::vec3 velocityMin = glm::vec3(0.0f);
    glm::vec3 velocityMax = glm::vec3(0.0f);

    // === NEW: Angular Velocity (radians/sec). Direction = axis, magnitude = speed ===
    glm::vec3 spawnAngularVelocity = glm::vec3(0.0f);
    bool randomizeAngularVelocity = false;
    glm::vec3 angularVelocityMin = glm::vec3(0.0f);
    glm::vec3 angularVelocityMax = glm::vec3(0.0f);

    bool randomizeScale = false;
    glm::vec3 scaleMin = glm::vec3(1.0f);
    glm::vec3 scaleMax = glm::vec3(1.0f);

    float spawnMass = 1.0f;
    float spawnLifespanSeconds = -1.0f; // -1 = no auto-despawn timer
    int spawnedCount = 0;

    // New fields for area spawning
    bool randomizePosition = false;
    glm::vec3 randomPosMin = glm::vec3(0.0f);
    glm::vec3 randomPosMax = glm::vec3(0.0f);

    // Attachment
    bool attachToTarget = false;
    std::string attachTargetName = ""; // Empty = use active camera
};

struct SpawnedFromSpawnerComponent {
    Entity sourceSpawner = MAX_ENTITIES;
};

struct DespawnerComponent {
    bool enabled = true;
    float remainingLifetimeSeconds = -1.0f; // -1 = collision-only despawner behavior
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
    bool noclipEnabled = true;
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

// 11. Ownership Tracking for Distributed Simulation
enum class ObjectOwnershipType : uint8_t {
    ONE = 0,
    TWO = 1,
    THREE = 2,
    FOUR = 3
};

struct OwnershipComponent {
    ObjectOwnershipType owner = ObjectOwnershipType::ONE;

    uint8_t GetOwnerIndex() const { return static_cast<uint8_t>(owner); }
    std::string GetOwnerName() const {
        switch (owner) {
            case ObjectOwnershipType::ONE: return "Player 1";
            case ObjectOwnershipType::TWO: return "Player 2";
            case ObjectOwnershipType::THREE: return "Player 3";
            case ObjectOwnershipType::FOUR: return "Player 4";
            default: return "Unknown";
        }
    }
    glm::vec4 GetOwnerColor() const {
        switch (owner) {
            case ObjectOwnershipType::ONE: return glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            case ObjectOwnershipType::TWO: return glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
            case ObjectOwnershipType::THREE: return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            case ObjectOwnershipType::FOUR: return glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
            default: return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        }
    }
};
