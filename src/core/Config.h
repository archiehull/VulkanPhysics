#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include "CoreTypes.h"

struct SceneOption {
    std::string name;
    std::string path;
};

// --- NEW: Custom Particle Configuration ---
struct CustomParticleConfig {
    std::string name;
    std::string texturePath;
    float rate = 100.0f;
    float lifeTime = 1.0f;
    bool isAdditive = false;
    glm::vec3 posVar = glm::vec3(0.0f);
    glm::vec3 vel = glm::vec3(0.0f);
    glm::vec3 velVar = glm::vec3(0.0f);
    glm::vec4 colorBegin = glm::vec4(1.0f);
    glm::vec4 colorEnd = glm::vec4(1.0f);
    glm::vec3 size = glm::vec3(1.0f, 1.0f, 0.0f); // x=begin, y=end, z=var
};

struct AttachedParticleConfig {
    std::string particleName;
    float duration = -1.0f; // -1 for infinite
};

struct SeasonConfig {
    float summerBaseTemp = 50.0f;
    float winterBaseTemp = -5.0f;
    float dayNightTempDiff = 35.0f;
};

struct TimeConfig {
    float dayLengthSeconds = 60.0f;
    int daysPerSeason = 3;
};

struct WeatherConfig {
    float minClearInterval = 30.0f;
    float maxClearInterval = 60.0f;
    float minPrecipitationDuration = 20.0f;
    float maxPrecipitationDuration = 40.0f;
    float fireSuppressionDuration = 15.0f;
};

// --- Entity Configuration Structs ---
struct SceneObjectConfig {
    std::string name;
    std::string type;
    std::string modelPath;
    std::string texturePath;

    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    glm::vec3 params = glm::vec3(0.0f);

    bool visible = true;
    bool castsShadow = true;
    bool receiveShadows = true;
    int shadingMode = 1;
    int layerMask = SceneLayers::ALL_USED;
    int onlyInRegionMask = 0;

    bool hasCollision = true;
    bool isStatic = true;
    bool isFlammable = false;

    float mass = 1.0f;
    float restitution = 1.0f;
    float friction = 0.98f;

    int colliderType = 0;
    float colliderRadius = 2.0f;
    glm::vec3 colliderNormal = glm::vec3(0.0f, 1.0f, 0.0f);

    bool hasOrbit = false;
    float orbitRadius = 0.0f;
    float orbitSpeed = -1.0f;
    float orbitDirection = 0.0f;
    float orbitInitialAngle = 0.0f;

    bool isLight = false;
    glm::vec3 lightColor = glm::vec3(1.0f);
    float lightIntensity = 1.0f;
    int lightType = 0;

    std::vector<AttachedParticleConfig> attachedParticles;

    TimeConfig timeConfig;
    SeasonConfig seasonConfig;
    WeatherConfig weatherConfig;
    float sunHeatBonus = 60.0f;

    bool isActive = false;
    glm::vec3 direction = glm::vec3(1.0f, 0.0f, 0.0f);
    float speed = 15.0f;

    bool spawnerEnabled = true;
    float spawnInterval = 1.0f;
    float spawnerRunDurationSeconds = -1.0f;
    int spawnerMaxSpawnsPerRun = -1;
    std::string spawnGeometryType = "Sphere";
    std::string spawnModelPath = "";
    std::string spawnTexturePath = "textures/default.jpg";
    glm::vec3 spawnScale = glm::vec3(1.0f);
    glm::vec3 spawnVelocity = glm::vec3(0.0f, 10.0f, 0.0f);
    bool randomizeSpawnVelocity = false;
    glm::vec3 spawnVelocityRandomRange = glm::vec3(0.0f);
    float spawnMass = 1.0f;

    bool isDespawner = false;
};

// --- Procedural Texture Configuration ---
struct ProceduralTextureConfig {
    std::string name;
    std::string type; // "Checker", "Gradient", "Solid"

    glm::vec4 color1 = glm::vec4(1.0f);
    glm::vec4 color2 = glm::vec4(0.0f);

    int width = 512;
    int height = 512;
    int cellSize = 64;   // For Checker
    bool isVertical = true; // For Gradient
};

struct ProceduralPlantConfig {
    std::string modelPath = "";
    std::string texturePath = "";
    float frequency = 1.0f;
    glm::vec3 minScale = glm::vec3(1.0f);
    glm::vec3 maxScale = glm::vec3(1.0f);
    glm::vec3 baseRotation = glm::vec3(0.0f);
    bool isFlammable = false;
};

struct CustomCameraConfig {
    std::string name;
    std::string type;         // "FreeRoam", "Orbit", "RandomTarget"
    std::string actionBind;   // e.g., "Camera1", "Camera2"
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 target = glm::vec3(0.0f);     // For Orbit
    float orbitRadius = 350.0f;             // For Orbit / RandomTarget
    std::string targetMatch;  // For RandomTarget (e.g., "cactus")
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct LayerRegionConfig {
    std::string name;
    int assignedLayerBit = 0;
    int volumeType = 0; // 0 = Sphere, 1 = Box (AABB)
    float radius = 10.0f;
    glm::vec3 halfExtents = glm::vec3(5.0f, 5.0f, 5.0f);
    glm::vec3 position = glm::vec3(0.0f); // Where this region exists in the world
};

struct AppConfig {
    int windowWidth = 1000;
    int windowHeight = 750;


    int proceduralObjectCount = 5;
    std::vector<ProceduralPlantConfig> proceduralPlants;
    std::vector<SceneObjectConfig> sceneObjects;
    std::vector<CustomCameraConfig> customCameras;
    std::vector<ProceduralTextureConfig> proceduralTextures;
    std::vector<CustomParticleConfig> customParticles;
    std::vector<LayerRegionConfig> layerRegions;
    std::unordered_map<std::string, std::string> inputBindings;
};

class ConfigLoader final {
public:
    static AppConfig Load(const std::string& directory = "config/");

    static std::vector<SceneOption> GetAvailableScenes(const std::string& rootDir = "src/config/");
private:
    static void ParseFile(AppConfig& config, const std::string& filepath);
};