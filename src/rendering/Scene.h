#pragma once

#include "../geometry/Geometry.h"
#include "../geometry/GeometryGenerator.h"
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <string>
#include "../vulkan/UniformBufferObject.h"
#include "../core/Config.h" 
#include "ParticleSystem.h"
#include "Camera.h"


// ECS Includes
#include "../core/CoreTypes.h"
#include "../core/ECS.h"
#include "../core/Components.h"
#include <unordered_map>
#include "../systems/ISystem.h"

struct TerrainConfig {
    bool exists = false;
    float radius = 100.0f;
    float heightScale = 1.0f;
    float noiseFreq = 0.01f;
    glm::vec3 position = glm::vec3(0.0f);
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

    Entity GetEntityByName(const std::string& name) const;

    void Initialize();

    float RadiusAdjustment(const float radius, const float deltaY) const;

    void AddTerrain(const std::string& name, float radius, int rings, int segments, float heightScale, float noiseFreq, const glm::vec3& position, const std::string& texturePath);

    void AddCube(const std::string& name, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const std::string& texturePath = "");
    void AddGrid(const std::string& name, int rows, int cols, float cellSize = 0.1f, const glm::vec3& position = glm::vec3(0.0f), const std::string& texturePath = "");
    void AddSphere(const std::string& name, int stacks = 16, int slices = 32, float radius = 0.5f, const glm::vec3& position = glm::vec3(0.0f), const std::string& texturePath = "");
    void AddGeometry(const std::string& name, std::unique_ptr<Geometry> geometry, const glm::vec3& position = glm::vec3(0.0f));

    void AddModel(const std::string& name, const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale, const std::string& modelPath, const std::string& texturePath, bool isFlammable = false);

    Entity AddLight(const std::string& name, const glm::vec3& position, const glm::vec3& color, float intensity, int type);
    Entity CreateCameraEntity(const std::string& name, const glm::vec3& pos, const std::string& type);

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

    void StopPrecipitation();
    void StopDust();
    void StopObjectFire(Entity e);

    void Ignite(Entity e);

    const std::vector<std::unique_ptr<ParticleSystem>>& GetParticleSystems() const { return particleSystems; }

    void Update(float deltaTime);
    void ResetEnvironment();

    void ToggleGlobalShadingMode();

    void AddSimpleShadow(const std::string& objectName, float radius);
    void ToggleSimpleShadows();

    // Updated to query the component instead of a local boolean
    bool IsUsingSimpleShadows() const {
        if (m_EnvironmentEntity == MAX_ENTITIES) return false;
        return m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).useSimpleShadows;
    }

    std::vector<Light> GetLights() const;

    void Clear();

    // --- ECS Data Accessors ---
    const Registry& GetRegistry() const { return m_Registry; }
    Registry& GetRegistry() { return m_Registry; }
    const std::vector<Entity>& GetRenderableEntities() const { return m_RenderableEntities; }

    Entity GetEnvironmentEntity() const { return m_EnvironmentEntity; }

    ParticleSystem* GetOrCreateSystem(const ParticleProps& props);

    std::shared_ptr<Geometry> dustGeometryPrototype;
    std::string sootTexturePath = "textures/soot.jpg";

    void SetObjectTransform(const std::string& name, const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);
    void SetObjectVisible(const std::string& name, bool visible);
    void SetOrbitSpeed(const std::string& name, float speedRadPerSec);

    void SetObjectLayerMask(const std::string& name, int mask);
    void SetLightLayerMask(const std::string& name, int mask);

    void SetObjectCastsShadow(const std::string& name, bool casts);
    void SetObjectReceivesShadows(const std::string& name, bool receives);
    void SetObjectShadingMode(const std::string& name, int mode);

    void SetObjectTexture(const std::string& objectName, const std::string& texturePath);

    // --- Configuration Setters (These route to EnvironmentComponent) ---
    void SetTimeConfig(const TimeConfig& config);
    void SetWeatherConfig(const WeatherConfig& config);
    void SetSeasonConfig(const SeasonConfig& config);
    void SetSunHeatBonus(float bonus);

    void ToggleWeather();
    void NextSeason();

    void ClearProceduralRegistry();

    void Cleanup() { Clear(); }

    // --- Environment Component Getters ---
    float GetWeatherIntensity() const;
    std::string GetSeasonName() const;
    bool IsPrecipitating() const;
    float GetSunHeatBonus() const;
    float GetPostRainFireSuppressionTimer() const;
    const TimeConfig& GetTimeConfig() const;
    bool IsDustActive() const;

    void SetObjectPhysics(const std::string& name, bool isStatic, float mass);
    void SpawnPhysicsBall(const glm::vec3& pos, const glm::vec3& velocity);

	void SetObjectCollider(const std::string& name, int type, float radius, const glm::vec3& normal);

private:
    Registry m_Registry;
    std::vector<std::unique_ptr<ISystem>> m_Systems;
    std::unordered_map<std::string, Entity> m_EntityMap;
    std::vector<Entity> m_RenderableEntities;
    std::vector<Entity> m_LightEntities;

    // The singleton entity tracking global environment configurations
    Entity m_EnvironmentEntity = MAX_ENTITIES;

    Entity AddObjectInternal(const std::string& name, std::shared_ptr<Geometry> geometry, const glm::vec3& position, const std::string& texturePath, bool isFlammable);
    void CreateSimpleShadowEntity(Entity targetEntity);


    // Particle System state variables
    int m_RainEmitterId = -1;
    int m_SnowEmitterId = -1;

    int globalShadingMode = 1;

    TerrainConfig m_TerrainConfig;
    std::vector<ProceduralObjectConfig> proceduralRegistry;

    VkDevice device;
    VkPhysicalDevice physicalDevice;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    GraphicsPipeline* particlePipelineAdditive = nullptr;
    GraphicsPipeline* particlePipelineAlpha = nullptr;
    VkDescriptorSetLayout particleDescriptorLayout = VK_NULL_HANDLE;
    uint32_t framesInFlight = 2;

    std::vector<std::unique_ptr<ParticleSystem>> particleSystems;
};