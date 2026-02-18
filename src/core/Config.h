#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

// --- Entity Configuration Structs ---

struct SceneObjectConfig {
    std::string name;
    std::string type; // "Model", "Sphere", "Cube", "Pedestal", "Terrain", "Bowl", "Grid"

    // Core Resources
    std::string modelPath;
    std::string texturePath;

    // Transform
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    // Generic Parameters (Context depends on Type)
    // Terrain: x=Radius, y=HeightScale, z=NoiseFreq
    // Pedestal: x=TopRadius, y=BaseWidth, z=Height
    // Sphere/Bowl: x=Radius
    glm::vec3 params = glm::vec3(0.0f);

    // Rendering
    bool visible = true;
    bool castsShadow = true;
    bool receiveShadows = true;
    int shadingMode = 1; // 1 = Phong, 0 = Gouraud
    int layerMask = 3;   // 1=Inside, 2=Outside, 3=All

    // Physics / Gameplay
    bool hasCollision = true;
    bool isFlammable = false;

    // Orbit Component
    bool hasOrbit = false;
    float orbitRadius = 0.0f;
    float orbitSpeed = -1.0f;      // -1 implies "Use Day Cycle Speed"
    float orbitDirection = 0.0f;   // Degrees (converts to Axis)
    float orbitInitialAngle = 0.0f;

    // Light Component
    bool isLight = false;
    glm::vec3 lightColor = glm::vec3(1.0f);
    float lightIntensity = 1.0f;
    int lightType = 0;
};

struct ProceduralPlantConfig {
    std::string modelPath;
    std::string texturePath;
    float frequency;
    glm::vec3 minScale;
    glm::vec3 maxScale;
    glm::vec3 baseRotation;
    bool isFlammable;
};

struct CustomCameraConfig {
    std::string name;
    std::string type;
    glm::vec3 position;
    glm::vec3 target;
};

// --- Global Settings Structs ---

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

struct AppConfig {
    // Window
    int windowWidth = 800;
    int windowHeight = 600;

    // Environment
    TimeConfig time;
    SeasonConfig seasons;
    WeatherConfig weather;
    float sunHeatBonus = 60.0f;

    // Procedural Generation Settings
    int proceduralObjectCount = 75;
    std::vector<ProceduralPlantConfig> proceduralPlants;

    // Explicit Scene Objects
    std::vector<SceneObjectConfig> sceneObjects;

    // Cameras
    std::vector<CustomCameraConfig> customCameras;
};

class ConfigLoader final {
public:
    // Loads 'settings.cfg' and 'scene.cfg' from the directory
    static AppConfig Load(const std::string& directory = "config/");

private:
    static void ParseFile(AppConfig& config, const std::string& filepath);
};