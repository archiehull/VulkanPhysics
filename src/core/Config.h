#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

struct ProceduralPlantConfig {
    std::string modelPath;
    std::string texturePath;
    float frequency;
    glm::vec3 minScale;
    glm::vec3 maxScale;
    glm::vec3 baseRotation;
    bool isFlammable;
};

struct StaticObjectConfig {
    std::string name;
    std::string modelPath;
    std::string texturePath;
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    bool isFlammable;
};

struct CustomCameraConfig {
    std::string name;
    std::string type; // "FreeRoam", "Orbit", "Static"
    glm::vec3 position;
    glm::vec3 target;
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

struct OrbitConfig {
    float directionDegrees = 0.0f;
    float radius = 275.0f;
    float initialAngle = 0.0f;
};

struct AppConfig {
    // Window
    int windowWidth = 800;
    int windowHeight = 600;

    // Environment
    TimeConfig time;
    SeasonConfig seasons;
    WeatherConfig weather;

    OrbitConfig sunOrbit;
    OrbitConfig moonOrbit;

    // Thermodynamics
    float sunHeatBonus = 60.0f;

    // Terrain
    float terrainHeightScale = 3.5f;
    float terrainNoiseFreq = 0.02f;

    // Objects
    int proceduralObjectCount = 75;
    std::vector<ProceduralPlantConfig> proceduralPlants;
    std::vector<StaticObjectConfig> staticObjects;

    // --- Custom Cameras ---
    std::vector<CustomCameraConfig> customCameras;
};

class ConfigLoader final {
public:
    static AppConfig Load(const std::string& filepath);
};