#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
struct SceneOption {
    std::string name;
    std::string path;
};


// --- Entity Configuration Structs ---

struct SceneObjectConfig {
    std::string name;
    std::string type;
    std::string modelPath;
    std::string texturePath; // Can now be a file path OR a procedural name

    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    glm::vec3 params = glm::vec3(0.0f);

    bool visible = true;
    bool castsShadow = true;
    bool receiveShadows = true;
    int shadingMode = 1;
    int layerMask = 3;

    bool hasCollision = true;
    bool isFlammable = false;

    bool hasOrbit = false;
    float orbitRadius = 0.0f;
    float orbitSpeed = -1.0f;
    float orbitDirection = 0.0f;
    float orbitInitialAngle = 0.0f;

    bool isLight = false;
    glm::vec3 lightColor = glm::vec3(1.0f);
    float lightIntensity = 1.0f;
    int lightType = 0;
};

// --- NEW: Procedural Texture Configuration ---
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
    int windowWidth = 800;
    int windowHeight = 600;

    TimeConfig time;
    SeasonConfig seasons;
    WeatherConfig weather;
    float sunHeatBonus = 60.0f;

    int proceduralObjectCount = 5;
    std::vector<ProceduralPlantConfig> proceduralPlants;
    std::vector<SceneObjectConfig> sceneObjects;
    std::vector<CustomCameraConfig> customCameras;
    std::vector<ProceduralTextureConfig> proceduralTextures;
    std::unordered_map<std::string, std::string> inputBindings;
};

class ConfigLoader final {
public:
    static AppConfig Load(const std::string& directory = "config/");

    static std::vector<SceneOption> GetAvailableScenes(const std::string& rootDir = "src/config/");
private:
    static void ParseFile(AppConfig& config, const std::string& filepath);
};