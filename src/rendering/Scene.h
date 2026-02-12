#pragma once

#include "../geometry/Geometry.h"
#include "../geometry/GeometryGenerator.h"
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <string>
#include "../vulkan/UniformBufferObject.h"
#include "../core/Config.h" // Include Config for struct defs
#include "ParticleSystem.h"

struct OrbitData {
    bool isOrbiting = false;
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 1.0f;
    float speed = 1.0f; // radians per second
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f); // Normalized Orbit axis
    glm::vec3 startVector = glm::vec3(1.0f, 0.0f, 0.0f); // Defines the zero-angle position
    float initialAngle = 0.0f; // Initial angle offset (in radians)
    float currentAngle = 0.0f; // Internal state: current angle (in radians)
};

enum class ObjectState {
    NORMAL,
    HEATING,
    BURNING,
    BURNT,
    REGROWING
};

namespace SceneLayers {
    constexpr int INSIDE = 1 << 0;
    constexpr int OUTSIDE = 1 << 1;
    constexpr int ALL = INSIDE | OUTSIDE;
}

enum class Season {
    SUMMER,
    AUTUMN,
    WINTER,
    SPRING
};

struct SceneLight {
    std::string name;
    Light vulkanLight;
    OrbitData OrbitData;
    int layerMask = SceneLayers::INSIDE;
};

struct TerrainConfig {
    bool exists = false;
    float radius = 100.0f;
    float heightScale = 1.0f;
    float noiseFreq = 0.01f;
    glm::vec3 position = glm::vec3(0.0f);
};

struct SceneObject {
    std::string name;
    std::shared_ptr<Geometry> geometry;
    std::shared_ptr<Geometry> storedOriginalGeometry = nullptr;

    glm::mat4 storedOriginalTransform = glm::mat4(1.0f);
    glm::mat4 transform = glm::mat4(1.0f);
    bool visible = true;

    std::string originalTexturePath;
    std::string texturePath;

    int shadingMode = 1;
    bool castsShadow = true;
    bool receiveShadows = true;

    OrbitData OrbitData;
    int layerMask = SceneLayers::INSIDE;

    ObjectState state = ObjectState::NORMAL;
    bool isFlammable = false;

    int fireEmitterId = -1;
    int smokeEmitterId = -1;
    int fireLightIndex = -1;

    // Thermodynamics
    float currentTemp = 20.0f;
    float ignitionThreshold = 100.0f;
    float thermalResponse = 5.0f;
    float selfHeatingRate = 15.0f;

    // Timers
    float burnTimer = 0.0f;
    float maxBurnDuration = 10.0f;

    float regrowTimer = 0.0f;
    float dustDuration = 10.0f;

    // Visuals
    float burnFactor = 0.0f;
    int fireParticleSystemIndex = -1;
    int simpleShadowId = -1;


    // Collisions
    bool hasCollision = true;
    float collisionRadius = 2.0f;
    float collisionHeight = 5.0f;

    explicit SceneObject(std::shared_ptr<Geometry> geo, const std::string& texPath = "", const std::string& objName = "")
        : name(objName), geometry(std::move(geo)), originalTexturePath(texPath), texturePath(texPath) {
    }
};

struct ProceduralObjectConfig {
    std::string modelPath;
    std::string texturePath;
    float frequency;
    glm::vec3 minScale;
    glm::vec3 maxScale;
    glm::vec3 baseRotation;
    bool isFlammable = false;
};

class Scene final {
public:
    Scene(VkDevice vkDevice, VkPhysicalDevice physDevice);
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void Initialize();

    float RadiusAdjustment(const float radius, const float deltaY) const;

    void AddTerrain(const std::string& name, float radius, int rings, int segments, float heightScale, float noiseFreq, const glm::vec3& position, const std::string& texturePath);

    void AddCube(const std::string& name, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const std::string& texturePath = "");
    void AddGrid(const std::string& name, int rows, int cols, float cellSize = 0.1f, const glm::vec3& position = glm::vec3(0.0f), const std::string& texturePath = "");
    void AddSphere(const std::string& name, int stacks = 16, int slices = 32, float radius = 0.5f, const glm::vec3& position = glm::vec3(0.0f), const std::string& texturePath = "");
    void AddGeometry(const std::string& name, std::unique_ptr<Geometry> geometry, const glm::vec3& position = glm::vec3(0.0f));

    void AddModel(const std::string& name, const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale, const std::string& modelPath, const std::string& texturePath, bool isFlammable = false);

    int AddLight(const std::string& name, const glm::vec3& position, const glm::vec3& color, float intensity, int type);

    void SetObjectOrbit(const std::string& name, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad = 0.0f);
    void SetLightOrbit(const std::string& name, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad = 0.0f);

    void AddBowl(const std::string& name, float radius, int slices, int stacks, const glm::vec3& position, const std::string& texturePath);
    void AddPedestal(const std::string& name, float topRadius, float baseWidth, float height, const glm::vec3& position, const std::string& texturePath);

    void SetupParticleSystem(VkCommandPool commandPoolArg, VkQueue graphicsQueueArg,
        GraphicsPipeline* additivePipeline, GraphicsPipeline* alphaPipeline,
        VkDescriptorSetLayout layout, uint32_t framesInFlightArg);

    const TerrainConfig& GetTerrainConfig() const { return m_TerrainConfig; }
    void SetObjectCollision(const std::string& name, bool enabled);
    void SetObjectCollisionSize(const std::string& name, float radius, float height);

    // Procedural Generation API
    void RegisterProceduralObject(const std::string& modelPath, const std::string& texturePath, float frequency, const glm::vec3& minScale, const glm::vec3& maxScale, const glm::vec3& baseRotation = glm::vec3(0.0f), bool isFlammable = false);
    void GenerateProceduralObjects(int count, float terrainRadius, float deltaY, float heightScale, float noiseFreq);

    // Particle Methods
    void AddCampfire(const std::string& name, const glm::vec3& position, float scale);

    int AddFire(const glm::vec3& position, float scale);
    int AddSmoke(const glm::vec3& position, float scale);
    void AddRain();
    void AddSnow();
    void AddDust();
    void SpawnDustCloud();

    void Ignite(SceneObject* obj);

    void UpdateThermodynamics(float deltaTime, float sunIntensity);

    float GetWeatherIntensity() const { return m_WeatherIntensity; }
    std::string GetSeasonName() const;

    const std::vector<std::unique_ptr<ParticleSystem>>& GetParticleSystems() const { return particleSystems; }

    void Update(float deltaTime);
    void ResetEnvironment();

    void ToggleGlobalShadingMode();

    void AddSimpleShadow(const std::string& objectName, float radius);
    void ToggleSimpleShadows();
    bool IsUsingSimpleShadows() const { return m_UseSimpleShadows; }

    std::vector<Light> GetLights() const;

    void Clear();
    const std::vector<std::unique_ptr<SceneObject>>& GetObjects() const { return objects; }

    void SetObjectTransform(size_t index, const glm::mat4& transform);
    void SetObjectVisible(size_t index, bool visible);
    void SetOrbitSpeed(const std::string& name, float speedRadPerSec);

    void SetObjectLayerMask(const std::string& name, int mask);
    void SetLightLayerMask(const std::string& name, int mask);

    void SetObjectCastsShadow(const std::string& name, bool casts);
    void SetObjectReceivesShadows(const std::string& name, bool receives);
    void SetObjectShadingMode(const std::string& name, int mode);

    // --- Configuration Setters ---
    void SetTimeConfig(const TimeConfig& config);
    void SetWeatherConfig(const WeatherConfig& config);
    void SetSeasonConfig(const SeasonConfig& config);
    void SetSunHeatBonus(float bonus);

    void ToggleWeather();
    bool IsPrecipitating() const { return m_IsPrecipitating; }
    void NextSeason();

    void ClearProceduralRegistry();

    void Cleanup() { Clear(); }

private:
    void AddObjectInternal(const std::string& name, std::unique_ptr<Geometry> geometry, const glm::vec3& position, const std::string& texturePath, bool isFlammable);
    void StopObjectFire(SceneObject* obj);

    glm::vec3 InitializeOrbit(OrbitData& data, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad) const;

    // --- Config Data ---
    TimeConfig m_TimeConfig;
    SeasonConfig m_SeasonConfig;
    WeatherConfig m_WeatherConfig;
    float m_SunHeatBonus = 60.0f;

    Season m_CurrentSeason = Season::SUMMER;
    float m_SeasonTimer = 0.0f;
    float m_WeatherIntensity = 0.0f;

    int m_RainEmitterId = -1;
    int m_SnowEmitterId = -1;

    bool m_IsPrecipitating = false;
    float m_WeatherTimer = 0.0f;
    float m_CurrentWeatherDurationTarget = 10.0f; // Target for current state (either clear or rain)
    float m_PostRainFireSuppressionTimer = 0.0f;

    void StopPrecipitation();
    void PickNextWeatherDuration(); // Helper to choose random duration

    bool m_DustActive = false;
    int m_DustEmitterId = -1;
    glm::vec3 m_DustPosition = glm::vec3(0.0f);
    glm::vec3 m_DustDirection = glm::vec3(0.0f);

    float m_TimeSinceLastRain = 0.0f;

    void StopDust();

    int globalShadingMode = 1;

    void UpdateSimpleShadows();
    bool m_UseSimpleShadows = false;

    TerrainConfig m_TerrainConfig;
    std::vector<SceneLight> m_SceneLights;
    std::vector<std::unique_ptr<SceneObject>> objects;
    std::vector<ProceduralObjectConfig> proceduralRegistry;

    VkDevice device;
    VkPhysicalDevice physicalDevice;

    ParticleSystem* GetOrCreateSystem(const ParticleProps& props);

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    GraphicsPipeline* particlePipelineAdditive = nullptr;
    GraphicsPipeline* particlePipelineAlpha = nullptr;
    VkDescriptorSetLayout particleDescriptorLayout = VK_NULL_HANDLE;
    uint32_t framesInFlight = 2;

    std::vector<std::unique_ptr<ParticleSystem>> particleSystems;

    std::shared_ptr<Geometry> dustGeometryPrototype;
    std::string sootTexturePath = "textures/soot.jpg";
};