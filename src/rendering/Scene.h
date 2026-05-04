#pragma once
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "../core/ECS.h"
#include "../core/Components.h"
#include "../core/CoreTypes.h"
#include "../core/Config.h"
#include "../geometry/Geometry.h"
#include "../vulkan/VulkanContext.h"
#include "../vulkan/UniformBufferObject.h"
#include "ParticleSystem.h"
#include <mutex>

struct TerrainConfig {
    bool exists = false;
    float radius = 0.0f;
    float heightScale = 0.0f;
    float noiseFreq = 0.0f;
    glm::vec3 position = glm::vec3(0.0f);
};

struct ProceduralObjectConfig {
    std::string modelPath;
    std::string texturePath;
    float frequency;
    glm::vec3 minScale;
    glm::vec3 maxScale;
    glm::vec3 baseRotation;
    bool isFlammable;
};

class Scene {
public:
    Scene(VkDevice device, VkPhysicalDevice physicalDevice);
    ~Scene();

    void RegisterEntityName(const std::string& name, Entity entity);
    Entity GetEntityByName(const std::string& name) const;
    void DeleteEntity(Entity entity);
    void RemoveRenderComponent(Entity entity);

    void Initialize();

    void CreateEnvironment(const std::string& name = "GlobalEnvironment");
    Entity CreateSpawnerEntity(const std::string& name, const glm::vec3& position);
    Entity AddSpawner(const std::string& name, const glm::vec3& position);
    Entity CreateDeathWall(const std::string& name, float yLevel, float halfWidth, float halfDepth);

    float RadiusAdjustment(const float radius, const float deltaY) const;

    void AddTerrain(const std::string& name, float radius, int rings, int segments, float heightScale, float noiseFreq, const glm::vec3& position, const std::string& texturePath);

    Entity AddCube(const std::string& name, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const std::string& texturePath = "");
    Entity AddPlane(const std::string& name, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const std::string& texturePath = "");
    void AddGrid(const std::string& name, int rows, int cols, float cellSize = 0.1f, const glm::vec3& position = glm::vec3(0.0f), const std::string& texturePath = "");
    Entity AddSphere(const std::string& name, int stacks = 16, int slices = 32, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const std::string& texturePath = "");
    Entity AddCylinder(const std::string& name, int slices = 32, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const std::string& texturePath = "");
    Entity AddDisk(const std::string& name, int slices = 32, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), const std::string& texturePath = "");
    void AddGeometry(const std::string& name, std::unique_ptr<Geometry> geometry, const glm::vec3& position = glm::vec3(0.0f));
    Entity AddCapsule(const std::string& name, float radius, float height, int radialSegments, int rings, 
                      const glm::vec3& position, const glm::vec3& scale, const std::string& texturePath = "");

    Entity AddObjectExplicit(Entity id, const std::string& name, std::shared_ptr<Geometry> geometry, const glm::vec3& position, const std::string& texturePath, bool isFlammable);
    Entity AddModel(const std::string& name, const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale, const std::string& modelPath, const std::string& texturePath, bool isFlammable, Entity explicitId = MAX_ENTITIES);

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
    void UpdatePhysics(float deltaTime);
    void UpdateVisuals(float deltaTime);
    void ResetEnvironment();

    // Deletion callback for network synchronization
    using EntityDeletedCallback = std::function<void(Entity)>;
    void SetEntityDeletedCallback(EntityDeletedCallback callback) { m_EntityDeletedCallback = callback; }

    // Return smoothed per-system timings (thread-safe copy)
    std::vector<std::pair<std::string, float>> GetSmoothedSystemTimings() const;
    std::vector<std::string> GetSystemNamesOrdered() const;
    std::unordered_map<std::string, float> GetSmoothedTimingsMap() const;
    float GetLastPhysicsTime() const { return m_LastPhysicsTime; }

    void ToggleGlobalShadingMode();

    void AddSimpleShadow(const std::string& objectName, float radius);
    void ToggleSimpleShadows();

    bool IsUsingSimpleShadows() const {
        if (m_EnvironmentEntity == MAX_ENTITIES) return false;
        return m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).useSimpleShadows;
    }

    std::vector<Light> GetLights() const;

    void Clear();

    const Registry& GetRegistry() const { return m_Registry; }
    Registry& GetRegistry() { return m_Registry; }
    const std::vector<Entity>& GetRenderableEntities() const { return m_RenderableEntities; }

    Entity GetEnvironmentEntity() const { return m_EnvironmentEntity; }

    ParticleSystem* GetOrCreateSystem(const ParticleProps& props);

    std::shared_ptr<Geometry> dustGeometryPrototype;
    std::string sootTexturePath = "textures/soot.jpg";

    void SetObjectTransform(const std::string& name, const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);
    void RegisterRenderableEntity(Entity entity);
    void SetObjectVisible(const std::string& name, bool visible);
    void SetOrbitSpeed(const std::string& name, float speedRadPerSec);

    void SetObjectLayerMask(const std::string& name, int mask);
    void SetObjectRegionVisibilityMasks(const std::string& name, int onlyInRegionMask);
    void SetLightLayerMask(const std::string& name, int mask);
    void SetLightFlicker(const std::string& name, bool enabled, float amount, int preset);

    void SetObjectCastsShadow(const std::string& name, bool casts);
    void SetObjectReceivesShadows(const std::string& name, bool receives);
    void SetObjectShadingMode(const std::string& name, int mode);

    void SetObjectTexture(const std::string& objectName, const std::string& texturePath);

    void SetTimeConfig(const TimeConfig& config);
    void SetWeatherConfig(const WeatherConfig& config);
    void SetSeasonConfig(const SeasonConfig& config);
    void SetSunHeatBonus(float bonus);

    void ToggleWeather();
    void NextSeason();

    void ClearProceduralRegistry();

    void Cleanup() { Clear(); }

    float GetWeatherIntensity() const;
    std::string GetSeasonName() const;
    bool IsPrecipitating() const;
    float GetSunHeatBonus() const;
    float GetPostRainFireSuppressionTimer() const;
    const TimeConfig& GetTimeConfig() const;
    bool IsDustActive() const;

    void SetObjectPhysics(const std::string& name, bool isStatic, float mass);
    void SetObjectPhysicsMaterial(const std::string& name, float restitution, float friction);
    void SpawnPhysicsBall(const glm::vec3& pos, const glm::vec3& velocity);
    void ResetSpawnerSpawnedObjects();

	void SetObjectCollider(const std::string& name, int type, float radius, const glm::vec3& normal);

    void InvalidateEnvironmentEntity(Entity e);

    Entity AddLayerRegion(const std::string& name, int layerBit, int volumeType, float radius, const glm::vec3& halfExtents, const glm::vec3& position);

    void SetRegionsOnlyDebugView(bool enabled) { m_RegionsOnlyDebugView = enabled; }
    bool GetRegionsOnlyDebugView() const { return m_RegionsOnlyDebugView; }

    void SetSpringVisualizationEnabled(bool enabled);
    bool GetSpringVisualizationEnabled() const { return m_ShowSpringVisuals; }
    void SetSpawnerVisualizationEnabled(bool enabled);
    bool GetSpawnerVisualizationEnabled() const { return m_ShowSpawnerVisuals; }

    bool IsLookaheadMode() const { return m_IsLookaheadMode; }
    void SetLookaheadMode(bool mode) { m_IsLookaheadMode = mode; }
    void DeactivateEntityForLookahead(Entity e);

    Entity CreateDustCloud(const std::string& name, const glm::vec3& position, const glm::vec3& direction, float speed, bool isActive);

private:
    void FlushDeferredGeometryCleanup();
    void UpdateSpringVisuals();
    void ClearSpringVisuals();
    glm::vec4 ComputeSpringVisualColor(float currentLength, float restLength) const;
    void UpdatePathVisuals();
    void ClearPathVisuals();
    void UpdateSpawnerVisuals();
    void ClearSpawnerVisuals();

    Registry m_Registry;
    std::vector<std::unique_ptr<class ISystem>> m_Systems;
    std::unordered_map<std::string, Entity> m_EntityMap;
    std::vector<Entity> m_RenderableEntities;

    Entity m_EnvironmentEntity = MAX_ENTITIES;

    EntityDeletedCallback m_EntityDeletedCallback = nullptr;

    Entity AddObjectInternal(const std::string& name, std::shared_ptr<Geometry> geometry, const glm::vec3& position, const std::string& texturePath, bool isFlammable, Entity explicitId = MAX_ENTITIES);
    void CreateSimpleShadowEntity(Entity targetEntity);

    int m_RainEmitterId = -1;
    int m_SnowEmitterId = -1;

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
    std::vector<std::shared_ptr<Geometry>> m_DeferredGeometryCleanup;
    std::vector<Entity> m_SpringVisualEntitiesList;
    std::vector<Entity> m_SpawnerVisualEntitiesList;
    std::vector<Entity> m_PathVisualEntitiesList;
    std::shared_ptr<Geometry> m_SpringVisualGeometry;
    std::shared_ptr<Geometry> m_SpawnerVisualGeometry;
    std::shared_ptr<Geometry> m_PathVisualGeometry;

    Entity CreateSpringVisualEntity();
    Entity CreateSpawnerVisualEntity();
    Entity CreatePathVisualEntity();

    bool m_ShowSpringVisuals = false;
    bool m_ShowSpawnerVisuals = false;
    bool m_IsLookaheadMode = false;
    bool m_RegionsOnlyDebugView = false;
    float m_ElapsedTime = 0.0f;
    int globalShadingMode = 1;

    mutable std::vector<std::pair<std::string, float>> m_LastSystemTimings;
    std::vector<std::pair<std::string, float>> m_LastPhysicsTimings;
    std::vector<std::pair<std::string, float>> m_LastVisualTimings;
    mutable std::mutex m_TimingsMutex;
    mutable std::unordered_map<std::string, float> m_SmoothedTimings;
    float m_SmoothingAlpha = 0.08f; // EMA alpha: lower = smoother
    float m_LastPhysicsTime = 0.0f;
};
