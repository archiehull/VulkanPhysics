#include "Scene.h"
#include "ParticleLibrary.h"
#include "../geometry/OBJLoader.h"
#include "../geometry/SJGLoader.h"
#include <glm/gtc/matrix_transform.hpp> 
#include <glm/gtc/quaternion.hpp>
#include <glm/common.hpp>
#include <iostream>
#include <algorithm>
#include <random>
#include <cmath>
#include <unordered_set>
#include "../util/AnimationMath.h"

namespace {
float ComputeFlickerPresetValue(int preset, float t, float phase) {
    switch (preset) {
    case 1: { // Fire
        const float f = 0.55f
            + 0.25f * std::sin(t * 17.0f + phase)
            + 0.15f * std::sin(t * 31.0f + phase * 1.7f)
            + 0.10f * std::sin(t * 57.0f + phase * 0.73f);
        return glm::clamp(f, 0.0f, 1.0f);
    }
    case 2: { // Candle
        const float f = 0.72f
            + 0.18f * std::sin(t * 7.0f + phase)
            + 0.10f * std::sin(t * 13.0f + phase * 0.61f);
        return glm::clamp(f, 0.0f, 1.0f);
    }
    case 3: { // Faulty / electrical stutter
        const float smooth = 0.65f + 0.35f * std::sin(t * 9.0f + phase);
        const float stutter = std::sin(t * 43.0f + phase * 2.1f);
        const float dip = (stutter > 0.84f) ? 0.12f : 1.0f;
        return glm::clamp(smooth * dip, 0.0f, 1.0f);
    }
    case 4: // Pulse
        return 0.5f + 0.5f * std::sin(t * 3.0f + phase);
    default:
        return 1.0f;
    }
}

}

// ECS Systems
#include "../systems/OrbitSystem.h"
#include "../systems/SimpleShadowSystem.h"
#include "../systems/ThermodynamicsSystem.h"
#include "../systems/TimeSystem.h"
#include "../systems/WeatherSystem.h"
#include "../systems/ParticleUpdateSystem.h"
#include "../systems/CameraSystem.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/ObjectSpawnerSystem.h"
#include "../systems/AnimationSystem.h"

Entity Scene::AddLayerRegion(const std::string& name, int layerBit, int volumeType, float radius, const glm::vec3& halfExtents, const glm::vec3& position) {
    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    m_Registry.AddComponent<NameComponent>(entity, { name });

    TransformComponent transform;
    transform.position = position;
    transform.UpdateMatrix();
    m_Registry.AddComponent<TransformComponent>(entity, transform);

    LayerRegionComponent region;
    region.layerName = name;
    region.assignedLayerBit = layerBit;
    region.volumeType = volumeType;
    region.radius = radius;
    region.halfExtents = halfExtents;

    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<float> colorDist(0.2f, 1.0f);
    region.regionDebugColor = glm::vec4(colorDist(rng), colorDist(rng), colorDist(rng), 0.25f);

    m_Registry.AddComponent<LayerRegionComponent>(entity, region);

    // Sync config-defined regions into global layer UI state
    if (layerBit >= 0 && layerBit < SceneLayers::MAX_LAYERS) {
        SceneLayers::ActiveLayerCount = std::max(SceneLayers::ActiveLayerCount, layerBit + 1);
        if (!name.empty()) {
            SceneLayers::LayerNames[layerBit] = name;
        }
    }

    return entity;
}

Entity Scene::CreateDeathWall(const std::string& name, float yLevel, float halfWidth, float halfDepth) {
    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    m_Registry.AddComponent<NameComponent>(entity, { name });

    TransformComponent transform;
    transform.position = glm::vec3(0.0f, yLevel, 0.0f);
    transform.UpdateMatrix();
    m_Registry.AddComponent<TransformComponent>(entity, transform);

    ColliderComponent collider;
    collider.hasCollision = true;
    collider.type = 1; // Plane
    collider.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    collider.radius = std::max(0.0f, halfWidth);
    collider.height = std::max(0.0f, halfDepth);
    m_Registry.AddComponent<ColliderComponent>(entity, collider);

    PhysicsComponent physics;
    physics.isStatic = true;
    physics.SetMass(0.0f);
    m_Registry.AddComponent<PhysicsComponent>(entity, physics);

    m_Registry.AddComponent<DespawnerComponent>(entity, DespawnerComponent{});

    return entity;
}

void Scene::CreateEnvironment(const std::string& name) {
    if (m_EnvironmentEntity != MAX_ENTITIES && m_Registry.HasComponent<EnvironmentComponent>(m_EnvironmentEntity)) {
        std::cout << "Environment already exists!" << std::endl;
        return;
    }

    if (m_EnvironmentEntity != MAX_ENTITIES) {
        m_EnvironmentEntity = MAX_ENTITIES;
    m_RegionsOnlyDebugView = false;
    }

    m_EnvironmentEntity = m_Registry.CreateEntity();
    m_Registry.AddComponent<NameComponent>(m_EnvironmentEntity, { name });
    m_Registry.AddComponent<EnvironmentComponent>(m_EnvironmentEntity, EnvironmentComponent{});
    m_EntityMap[name] = m_EnvironmentEntity;
}

void Scene::SetObjectPhysics(const std::string& name, bool isStatic, float mass) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES) {
        if (!m_Registry.HasComponent<PhysicsComponent>(e)) {
            m_Registry.AddComponent<PhysicsComponent>(e, PhysicsComponent{});
        }
        auto& phys = m_Registry.GetComponent<PhysicsComponent>(e);
        phys.isStatic = isStatic;
        phys.SetMass(isStatic ? 0.0f : mass);
    }
}

void Scene::SetObjectPhysicsMaterial(const std::string& name, float restitution, float friction) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<PhysicsComponent>(e)) {
        auto& phys = m_Registry.GetComponent<PhysicsComponent>(e);
        phys.restitution = glm::clamp(restitution, 0.0f, 1.0f);
        phys.friction = glm::clamp(friction, 0.0f, 1.0f);
    }
}

void Scene::SpawnPhysicsBall(const glm::vec3& pos, const glm::vec3& velocity) {
    static int ballCount = 0;
    std::string name = "DynamicBall_" + std::to_string(ballCount++);

    const Entity existingEntity = GetEntityByName(name);
    if (existingEntity != MAX_ENTITIES) {
        std::cout << "[EntityDebug] SpawnPhysicsBall name collision: '" << name
            << "' already mapped to entity " << existingEntity << std::endl;
    }

    AddSphere(name, 16, 16, 1.0f, pos, "textures/default.jpg");

    Entity e = GetEntityByName(name);
    std::cout << "[EntityDebug] SpawnPhysicsBall created name='" << name
        << "' entity=" << e
        << " entityCount=" << m_Registry.GetEntityCount() << std::endl;

    if (!m_Registry.HasComponent<PhysicsComponent>(e)) {
        m_Registry.AddComponent<PhysicsComponent>(e, PhysicsComponent{});
    }
    if (!m_Registry.HasComponent<ColliderComponent>(e)) {
        m_Registry.AddComponent<ColliderComponent>(e, ColliderComponent{});
    }

    auto& phys = m_Registry.GetComponent<PhysicsComponent>(e);
    phys.isStatic = false;
    phys.velocity = velocity;
    phys.restitution = 0.7f;
    phys.SetMass(2.0f);

    auto& col = m_Registry.GetComponent<ColliderComponent>(e);
    col.radius = 1.0f;
    col.hasCollision = true;
}

void Scene::ResetSpawnerSpawnedObjects() {
    std::vector<Entity> entitiesToDelete;

    for (Entity e = 0; e < m_Registry.GetEntityCount(); ++e) {
        if (m_Registry.HasComponent<SpawnedFromSpawnerComponent>(e)) {
            entitiesToDelete.push_back(e);
        }
    }

    for (Entity e : entitiesToDelete) {
        DeleteEntity(e);
    }

    for (Entity e = 0; e < m_Registry.GetEntityCount(); ++e) {
        if (m_Registry.HasComponent<ObjectSpawnerComponent>(e)) {
            auto& spawner = m_Registry.GetComponent<ObjectSpawnerComponent>(e);
            spawner.spawnTimer = 0.0f;
            spawner.runElapsedSeconds = 0.0f;
            spawner.spawnedThisRun = 0;
            spawner.spawnedCount = 0;
            spawner.isRunning = spawner.alwaysOn;
        }
    }
}

Entity Scene::GetEntityByName(const std::string& name) const {
    auto it = m_EntityMap.find(name);
    if (it != m_EntityMap.end()) {
        const Entity mapped = it->second;
        if (mapped < m_Registry.GetEntityCount() &&
            m_Registry.HasComponent<NameComponent>(mapped) &&
            m_Registry.GetComponent<NameComponent>(mapped).name == name) {
            return mapped;
        }
    }

    for (Entity e = 0; e < m_Registry.GetEntityCount(); ++e) {
        if (!m_Registry.HasComponent<NameComponent>(e)) {
            continue;
        }
        if (m_Registry.GetComponent<NameComponent>(e).name == name) {
            return e;
        }
    }

    return MAX_ENTITIES;
}

std::unique_ptr<Scene> Scene::CreateSimulationClone() const {
    auto clone = std::make_unique<Scene>(device, physicalDevice);

    clone->globalShadingMode = globalShadingMode;
    clone->m_TerrainConfig = m_TerrainConfig;
    clone->m_RegionsOnlyDebugView = m_RegionsOnlyDebugView;
    clone->m_ShowSpringVisuals = m_ShowSpringVisuals;
    clone->m_ElapsedTime = m_ElapsedTime;

    const Entity entityCount = m_Registry.GetEntityCount();
    for (Entity e = 0; e < entityCount; ++e) {
        clone->m_Registry.CreateEntity();
    }

    auto copyIfPresent = [&](auto dummyComponent) {
        using T = decltype(dummyComponent);
        for (Entity e = 0; e < entityCount; ++e) {
            if (m_Registry.HasComponent<T>(e)) {
                clone->m_Registry.AddComponent<T>(e, m_Registry.GetComponent<T>(e));
            }
        }
    };

    copyIfPresent(NameComponent{});
    copyIfPresent(TransformComponent{});
    copyIfPresent(RenderComponent{});
    copyIfPresent(OrbitComponent{});
    copyIfPresent(PathAnimationComponent{});
    copyIfPresent(ThermoComponent{});
    copyIfPresent(PhysicsComponent{});
    copyIfPresent(SpringComponent{});
    copyIfPresent(ColliderComponent{});
    copyIfPresent(LightComponent{});
    copyIfPresent(AttachedEmitterComponent{});
    copyIfPresent(EnvironmentComponent{});
    copyIfPresent(DustCloudComponent{});
    copyIfPresent(ObjectSpawnerComponent{});
    copyIfPresent(SpawnedFromSpawnerComponent{});
    copyIfPresent(DespawnerComponent{});
    copyIfPresent(CameraComponent{});
    copyIfPresent(LayerRegionComponent{});

    auto hasTrackedComponent = [&](Entity e) {
        return m_Registry.HasComponent<NameComponent>(e) ||
            m_Registry.HasComponent<TransformComponent>(e) ||
            m_Registry.HasComponent<RenderComponent>(e) ||
            m_Registry.HasComponent<OrbitComponent>(e) ||
            m_Registry.HasComponent<PathAnimationComponent>(e) ||
            m_Registry.HasComponent<ThermoComponent>(e) ||
            m_Registry.HasComponent<PhysicsComponent>(e) ||
            m_Registry.HasComponent<SpringComponent>(e) ||
            m_Registry.HasComponent<ColliderComponent>(e) ||
            m_Registry.HasComponent<LightComponent>(e) ||
            m_Registry.HasComponent<AttachedEmitterComponent>(e) ||
            m_Registry.HasComponent<EnvironmentComponent>(e) ||
            m_Registry.HasComponent<DustCloudComponent>(e) ||
            m_Registry.HasComponent<ObjectSpawnerComponent>(e) ||
            m_Registry.HasComponent<SpawnedFromSpawnerComponent>(e) ||
            m_Registry.HasComponent<DespawnerComponent>(e) ||
            m_Registry.HasComponent<CameraComponent>(e) ||
            m_Registry.HasComponent<LayerRegionComponent>(e);
    };

    for (Entity e = 0; e < entityCount; ++e) {
        if (!hasTrackedComponent(e)) {
            clone->m_Registry.DestroyEntity(e);
        }
    }

    for (Entity e = 0; e < entityCount; ++e) {
        if (clone->m_Registry.HasComponent<NameComponent>(e)) {
            clone->m_EntityMap[clone->m_Registry.GetComponent<NameComponent>(e).name] = e;
        }
        if (clone->m_Registry.HasComponent<RenderComponent>(e)) {
            clone->m_RenderableEntities.push_back(e);
        }
        if (clone->m_Registry.HasComponent<LightComponent>(e)) {
            clone->m_LightEntities.push_back(e);
        }
    }

    clone->m_EnvironmentEntity = MAX_ENTITIES;
    if (m_EnvironmentEntity != MAX_ENTITIES &&
        m_EnvironmentEntity < entityCount &&
        clone->m_Registry.HasComponent<EnvironmentComponent>(m_EnvironmentEntity)) {
        clone->m_EnvironmentEntity = m_EnvironmentEntity;
    }

    return clone;
}

void Scene::DeleteEntity(Entity entity) {
    if (entity == MAX_ENTITIES || entity >= m_Registry.GetEntityCount()) return;

    // Remove stale spring links to this entity so recycled entity IDs
    // do not get accidentally pulled by existing spring anchors.
    for (Entity e = 0; e < m_Registry.GetEntityCount(); ++e) {
        if (!m_Registry.HasComponent<SpringComponent>(e)) continue;
        auto& spring = m_Registry.GetComponent<SpringComponent>(e);
        spring.connectedEntities.erase(
            std::remove(spring.connectedEntities.begin(), spring.connectedEntities.end(), entity),
            spring.connectedEntities.end());
    }

    Entity linkedShadow = MAX_ENTITIES;
    if (m_Registry.HasComponent<RenderComponent>(entity)) {
        auto& render = m_Registry.GetComponent<RenderComponent>(entity);
        if (render.geometry) {
            // Defer GPU resource destruction to avoid hard stalls in runtime deletion paths.
            if (render.geometry.use_count() == 1) {
                m_DeferredGeometryCleanup.push_back(std::move(render.geometry));
            }
        }
        linkedShadow = render.simpleShadowEntity;
    }

    if (m_Registry.HasComponent<ThermoComponent>(entity)) {
        StopObjectFire(entity);
    }

    if (m_Registry.HasComponent<NameComponent>(entity)) {
        m_EntityMap.erase(m_Registry.GetComponent<NameComponent>(entity).name);
    }

    m_RenderableEntities.erase(std::remove(m_RenderableEntities.begin(), m_RenderableEntities.end(), entity), m_RenderableEntities.end());
    m_LightEntities.erase(std::remove(m_LightEntities.begin(), m_LightEntities.end(), entity), m_LightEntities.end());

    for (Entity e : m_RenderableEntities) {
        if (m_Registry.HasComponent<RenderComponent>(e)) {
            auto& render = m_Registry.GetComponent<RenderComponent>(e);
            if (render.simpleShadowEntity == entity) {
                render.simpleShadowEntity = MAX_ENTITIES;
            }
        }
    }

    if (entity == m_EnvironmentEntity) {
        m_EnvironmentEntity = MAX_ENTITIES;
    }

    m_Registry.DestroyEntity(entity);

    if (linkedShadow != MAX_ENTITIES && linkedShadow != entity) {
        DeleteEntity(linkedShadow);
    }
}

void Scene::RemoveRenderComponent(Entity entity) {
    if (entity == MAX_ENTITIES || entity >= m_Registry.GetEntityCount()) return;
    if (!m_Registry.HasComponent<RenderComponent>(entity)) return;

    auto& render = m_Registry.GetComponent<RenderComponent>(entity);
    const Entity shadowEntity = render.simpleShadowEntity;

    if (render.geometry) {
        // Defer GPU resource destruction to avoid hard stalls in runtime deletion paths.
        if (render.geometry.use_count() == 1) {
            m_DeferredGeometryCleanup.push_back(std::move(render.geometry));
        }
    }

    m_RenderableEntities.erase(std::remove(m_RenderableEntities.begin(), m_RenderableEntities.end(), entity), m_RenderableEntities.end());

    for (Entity e : m_RenderableEntities) {
        if (!m_Registry.HasComponent<RenderComponent>(e)) continue;
        auto& otherRender = m_Registry.GetComponent<RenderComponent>(e);
        if (otherRender.simpleShadowEntity == entity) {
            otherRender.simpleShadowEntity = MAX_ENTITIES;
        }
    }

    m_Registry.RemoveComponent<RenderComponent>(entity);

    if (shadowEntity != MAX_ENTITIES && shadowEntity != entity) {
        DeleteEntity(shadowEntity);
    }
}

void Scene::ToggleGlobalShadingMode() {
    globalShadingMode = (globalShadingMode == 1) ? 0 : 1;

    for (Entity e : m_RenderableEntities) {
        if (m_Registry.HasComponent<RenderComponent>(e)) {
            auto& renderComp = m_Registry.GetComponent<RenderComponent>(e);
            if (renderComp.shadingMode == 0 || renderComp.shadingMode == 1) {
                renderComp.shadingMode = globalShadingMode;
            }
        }
    }
    std::cout << "Shading Mode Toggled: " << (globalShadingMode == 1 ? "Phong" : "Gouraud") << std::endl;
}

Entity Scene::AddObjectInternal(const std::string& name, std::shared_ptr<Geometry> geometry, const glm::vec3& position, const std::string& texturePath, bool isFlammable) {
    const Entity preExistingEntity = GetEntityByName(name);
    if (preExistingEntity != MAX_ENTITIES) {
        std::cout << "[EntityDebug] AddObjectInternal duplicate name: '" << name
            << "' existingEntity=" << preExistingEntity << std::endl;
    }

    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    if (name.rfind("DynamicBall_", 0) == 0 || preExistingEntity != MAX_ENTITIES) {
        std::cout << "[EntityDebug] AddObjectInternal assigned entity=" << entity
            << " for name='" << name << "'" << std::endl;
    }

    m_Registry.AddComponent<NameComponent>(entity, { name });

    TransformComponent transform;
    transform.position = position;
    transform.UpdateMatrix();
    m_Registry.AddComponent<TransformComponent>(entity, transform);

    RenderComponent render;
    render.geometry = geometry;
    render.geometryName = name;
    render.texturePath = texturePath;
    render.originalTexturePath = texturePath;
    render.shadingMode = globalShadingMode;
    m_Registry.AddComponent<RenderComponent>(entity, render);

    m_RenderableEntities.push_back(entity);

    if (isFlammable) {
        ThermoComponent thermo;
        thermo.isFlammable = true;
        m_Registry.AddComponent<ThermoComponent>(entity, thermo);
    }

    return entity;
}

float Scene::RadiusAdjustment(const float radius, const float deltaY) const {
    const float planeY = deltaY;
    float terrainRadius = 0.0f;
    const float absDist = std::fabs(planeY);
    if (absDist < radius) {
        terrainRadius = std::sqrt(radius * radius - absDist * absDist);
    }
    else {
        terrainRadius = 0.0f;
    }
    return terrainRadius;
}

Scene::Scene(VkDevice vkDevice, VkPhysicalDevice physDevice)
    : device(vkDevice), physicalDevice(physDevice) {
}

void Scene::Initialize() {
    try {
        auto dustGeo = OBJLoader::Load(device, physicalDevice, "models/dust.obj");
        dustGeometryPrototype = std::shared_ptr<Geometry>(std::move(dustGeo));
    }
    catch (const std::exception& e) {
        throw std::runtime_error(std::string("Warning: Failed to load dust prototype: ") + e.what());
    }

    //// 1. Create Global Environment Entity
    //m_EnvironmentEntity = m_Registry.CreateEntity();
    //m_Registry.AddComponent<NameComponent>(m_EnvironmentEntity, { "GlobalEnvironment" });
    //m_Registry.AddComponent<EnvironmentComponent>(m_EnvironmentEntity, EnvironmentComponent{});

    //// 2. Create Global Dust Cloud Entity
    //Entity dustEntity = m_Registry.CreateEntity();
    //m_Registry.AddComponent<NameComponent>(dustEntity, { "GlobalDustCloud" });
    //m_Registry.AddComponent<DustCloudComponent>(dustEntity, DustCloudComponent{});

    m_SpringVisualGeometry = std::shared_ptr<Geometry>(GeometryGenerator::CreateCube(device, physicalDevice));


    m_EnvironmentEntity = MAX_ENTITIES;

    // 3. Register ECS Systems
    m_Systems.push_back(std::make_unique<CameraSystem>());
    m_Systems.push_back(std::make_unique<OrbitSystem>());
    m_Systems.push_back(std::make_unique<AnimationSystem>());
    m_Systems.push_back(std::make_unique<TimeSystem>());
    m_Systems.push_back(std::make_unique<WeatherSystem>());
    m_Systems.push_back(std::make_unique<ParticleUpdateSystem>());
    m_Systems.push_back(std::make_unique<SimpleShadowSystem>());
    m_Systems.push_back(std::make_unique<ThermodynamicsSystem>());
    m_Systems.push_back(std::make_unique<PhysicsSystem>());
    m_Systems.push_back(std::make_unique<ObjectSpawnerSystem>());
}

void Scene::RegisterProceduralObject(const std::string& modelPath, const std::string& texturePath, float frequency, const glm::vec3& minScale, const glm::vec3& maxScale, const glm::vec3& baseRotation, bool isFlammable) {
    ProceduralObjectConfig config;
    config.modelPath = modelPath;
    config.texturePath = texturePath;
    config.frequency = frequency;
    config.minScale = minScale;
    config.maxScale = maxScale;
    config.baseRotation = baseRotation;
    config.isFlammable = isFlammable;
    proceduralRegistry.push_back(config);
}

void Scene::GenerateProceduralObjects(int count, float terrainRadius, float deltaY, float heightScale, float noiseFreq) {
    if (proceduralRegistry.empty()) return;

    float totalFreq = 0.0f;
    for (const auto& item : proceduralRegistry) totalFreq += item.frequency;

    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> distFreq(0.0f, totalFreq);
    std::uniform_real_distribution<float> distScale(0.0f, 1.0f);
    std::uniform_real_distribution<float> distRot(0.0f, 360.0f);
    std::uniform_real_distribution<float> distThermal(0.5f, 10.0f);

    for (int i = 0; i < count; i++) {
        const float r = std::sqrt(distScale(gen)) * (terrainRadius * 0.9f);
        const float theta = distAngle(gen);
        const float x = r * cos(theta);
        const float z = r * sin(theta);

        const float yOffset = GeometryGenerator::GetTerrainHeight(x, z, terrainRadius, heightScale, noiseFreq);
        const float y = deltaY + yOffset;

        const float pick = distFreq(gen);
        float current = 0.0f;
        size_t selectedIndex = 0;
        for (size_t k = 0; k < proceduralRegistry.size(); ++k) {
            current += proceduralRegistry[k].frequency;
            if (pick <= current) {
                selectedIndex = k;
                break;
            }
        }
        const auto& config = proceduralRegistry[selectedIndex];

        glm::vec3 scale;
        scale.x = glm::mix(config.minScale.x, config.maxScale.x, distScale(gen));
        scale.y = glm::mix(config.minScale.y, config.maxScale.y, distScale(gen));
        scale.z = glm::mix(config.minScale.z, config.maxScale.z, distScale(gen));

        const std::string name = "ProcObj_" + std::to_string(i);
        AddModel(name, glm::vec3(x, y, z), glm::vec3(0.0f), scale, config.modelPath, config.texturePath, config.isFlammable);

        Entity mainObj = GetEntityByName(name);
        const float shadowRadius = std::max(std::max(scale.x, scale.z) * 1.5f, 0.5f);
        AddSimpleShadow(name, shadowRadius);

        if (mainObj != MAX_ENTITIES) {
            if (config.isFlammable && m_Registry.HasComponent<ThermoComponent>(mainObj)) {
                m_Registry.GetComponent<ThermoComponent>(mainObj).thermalResponse = distThermal(gen);
            }

            if (m_Registry.HasComponent<TransformComponent>(mainObj)) {
                auto& transform = m_Registry.GetComponent<TransformComponent>(mainObj);
                transform.position = glm::vec3(x, y, z);

                const float randomYaw = distRot(gen);
                transform.rotation = config.baseRotation + glm::vec3(0.0f, randomYaw, 0.0f);
                transform.scale = scale;
                transform.UpdateMatrix();
            }
        }
    }
}

void Scene::AddTerrain(const std::string& name, float radius, int rings, int segments, float heightScale, float noiseFreq, const glm::vec3& position, const std::string& texturePath) {
    auto geo = GeometryGenerator::CreateTerrain(device, physicalDevice, radius - 1, rings, segments, heightScale, noiseFreq);
    Entity entity = AddObjectInternal(name, std::move(geo), position, texturePath, false);
    m_Registry.GetComponent<RenderComponent>(entity).geometryName = "terrain";

    m_TerrainConfig.exists = true;
    m_TerrainConfig.radius = radius;
    m_TerrainConfig.heightScale = heightScale;
    m_TerrainConfig.noiseFreq = noiseFreq;
    m_TerrainConfig.position = position;
}

void Scene::AddBowl(const std::string& name, float radius, int slices, int stacks, const glm::vec3& position, const std::string& texturePath) {
    Entity entity = AddObjectInternal(name, GeometryGenerator::CreateBowl(device, physicalDevice, radius, slices, stacks), position, texturePath, false);
    m_Registry.GetComponent<RenderComponent>(entity).geometryName = "bowl";
}

void Scene::AddPedestal(const std::string& name, float topRadius, float baseWidth, float height, const glm::vec3& position, const std::string& texturePath) {
    auto geo = GeometryGenerator::CreatePedestal(device, physicalDevice, topRadius, baseWidth, height, 512, 512);
    Entity entity = AddObjectInternal(name, std::move(geo), position, texturePath, false);
    m_Registry.GetComponent<RenderComponent>(entity).geometryName = "pedestal";
}

void Scene::AddCube(const std::string& name, const glm::vec3& position, const glm::vec3& scale, const std::string& texturePath) {
    auto geo = GeometryGenerator::CreateCube(device, physicalDevice);
    Entity entity = AddObjectInternal(name, std::move(geo), position, texturePath, false);
    m_Registry.GetComponent<RenderComponent>(entity).geometryName = "cube";

    auto& transform = m_Registry.GetComponent<TransformComponent>(entity);
    transform.scale = scale;
    transform.UpdateMatrix();
}

void Scene::AddGrid(const std::string& name, int rows, int cols, float cellSize, const glm::vec3& position, const std::string& texturePath) {
    Entity entity = AddObjectInternal(name, GeometryGenerator::CreateGrid(device, physicalDevice, rows, cols, cellSize), position, texturePath, false);
    m_Registry.GetComponent<RenderComponent>(entity).geometryName = "grid";
}

void Scene::AddSphere(const std::string& name, int stacks, int slices, float radius, const glm::vec3& position, const std::string& texturePath) {
    Entity entity = AddObjectInternal(name, GeometryGenerator::CreateSphere(device, physicalDevice, stacks, slices, radius), position, texturePath, false);
    m_Registry.GetComponent<RenderComponent>(entity).geometryName = "sphere";
}

void Scene::AddGeometry(const std::string& name, std::unique_ptr<Geometry> geometry, const glm::vec3& position) {
    AddObjectInternal(name, std::move(geometry), position, "", false);
}

void Scene::AddModel(const std::string& name, const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale, const std::string& modelPath, const std::string& texturePath, bool isFlammable) {
    try {
        std::shared_ptr<Geometry> geometry;
        std::string ext = modelPath.substr(modelPath.find_last_of(".") + 1);
        if (ext == "sjg") {
            geometry = SJGLoader::Load(device, physicalDevice, modelPath);
        }
        else {
            geometry = OBJLoader::Load(device, physicalDevice, modelPath);
        }

        Entity entity = AddObjectInternal(name, geometry, position, texturePath, isFlammable);
        m_Registry.GetComponent<RenderComponent>(entity).geometryName = modelPath;

        auto& transform = m_Registry.GetComponent<TransformComponent>(entity);
        transform.position = position;
        transform.rotation = rotation;
        transform.scale = scale;
        transform.UpdateMatrix();
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to add model '" << modelPath << "': " << e.what() << std::endl;
    }
}

void Scene::CreateSimpleShadowEntity(Entity targetEntity) {
    if (!m_Registry.HasComponent<RenderComponent>(targetEntity)) return;

    float radius = m_Registry.GetComponent<RenderComponent>(targetEntity).simpleShadowRadius;
    if (radius <= 0.0f) return; // Doesn't need a shadow

    std::string targetName = "Unknown";
    if (m_Registry.HasComponent<NameComponent>(targetEntity)) {
        targetName = m_Registry.GetComponent<NameComponent>(targetEntity).name;
    }
    std::string shadowName = targetName + "_Shadow";

    auto diskGeo = GeometryGenerator::CreateDisk(device, physicalDevice, radius, 16);
    Entity shadowEntity = AddObjectInternal(shadowName, std::move(diskGeo), glm::vec3(0.0f), "textures/shadow.jpg", false);
    m_Registry.GetComponent<RenderComponent>(shadowEntity).geometryName = "disk";

    auto& shadowRender = m_Registry.GetComponent<RenderComponent>(shadowEntity);
    shadowRender.castsShadow = false;
    shadowRender.originalCastsShadow = false;
    shadowRender.receiveShadows = false;
    shadowRender.shadingMode = 0;
    // Match the parent object's visibility
    shadowRender.visible = m_Registry.GetComponent<RenderComponent>(targetEntity).visible;

    ThermoComponent shadowThermo;
    shadowThermo.burnFactor = 1.0f;
    m_Registry.AddComponent<ThermoComponent>(shadowEntity, shadowThermo);

    // Link it back to the parent
    m_Registry.GetComponent<RenderComponent>(targetEntity).simpleShadowEntity = shadowEntity;
}


// --- 2. UPDATE: AddSimpleShadow to just register the intent ---
void Scene::AddSimpleShadow(const std::string& objectName, float radius) {
    Entity targetEntity = GetEntityByName(objectName);
    if (targetEntity == MAX_ENTITIES) return;

    // Just save the requested radius
    auto& targetRender = m_Registry.GetComponent<RenderComponent>(targetEntity);
    targetRender.simpleShadowRadius = radius;

    // If shadows are ALREADY active when this object spawns, create it immediately
    if (IsUsingSimpleShadows()) {
        CreateSimpleShadowEntity(targetEntity);
    }
}


// --- 3. UPDATE: ToggleSimpleShadows to Spawn/Destroy dynamically ---
void Scene::ToggleSimpleShadows() {
    if (m_EnvironmentEntity == MAX_ENTITIES) return;
    auto& env = m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity);

    env.useSimpleShadows = !env.useSimpleShadows;

    // CRITICAL: Wait for the GPU to finish its current frame before destroying geometry!
    vkDeviceWaitIdle(device);

    // Make a copy of the list because we might add/remove entities while looping
    std::vector<Entity> entitiesToProcess = m_RenderableEntities;

    for (Entity e : entitiesToProcess) {
        if (!m_Registry.HasComponent<RenderComponent>(e)) continue;

        auto& render = m_Registry.GetComponent<RenderComponent>(e);

        if (env.useSimpleShadows) {
            // Turn ON Simple Shadows
            render.castsShadow = false;

            // Create the entity if it requests one but doesn't have one yet
            if (render.simpleShadowRadius > 0.0f && render.simpleShadowEntity == MAX_ENTITIES) {
                CreateSimpleShadowEntity(e);
            }
        }
        else {
            // Turn OFF Simple Shadows
            render.castsShadow = render.originalCastsShadow;

            // Fully Destroy the simple shadow entity if it exists
            if (render.simpleShadowEntity != MAX_ENTITIES) {
                Entity shadowEnt = render.simpleShadowEntity;

                // 1. Destroy Vulkan Geometry
                if (m_Registry.HasComponent<RenderComponent>(shadowEnt)) {
                    auto& sr = m_Registry.GetComponent<RenderComponent>(shadowEnt);
                    if (sr.geometry) sr.geometry->Cleanup();
                }

                // 2. Remove from Maps & Lists
                if (m_Registry.HasComponent<NameComponent>(shadowEnt)) {
                    m_EntityMap.erase(m_Registry.GetComponent<NameComponent>(shadowEnt).name);
                }
                auto it = std::find(m_RenderableEntities.begin(), m_RenderableEntities.end(), shadowEnt);
                if (it != m_RenderableEntities.end()) m_RenderableEntities.erase(it);

                // 3. Destroy in ECS
                m_Registry.DestroyEntity(shadowEnt);

                // 4. Sever the link
                render.simpleShadowEntity = MAX_ENTITIES;
            }
        }
    }
    std::cout << "Shadow Mode: " << (env.useSimpleShadows ? "Simple" : "Normal") << std::endl;
}

Entity Scene::AddLight(const std::string& name, const glm::vec3& position, const glm::vec3& color, float intensity, int type) {
    if (m_LightEntities.size() >= MAX_LIGHTS) {
        std::cerr << "Warning: Maximum number of lights reached." << std::endl;
        return MAX_ENTITIES;
    }

    Entity entity = GetEntityByName(name);

    if (entity == MAX_ENTITIES) {
        entity = m_Registry.CreateEntity();
        m_EntityMap[name] = entity;

        m_Registry.AddComponent<NameComponent>(entity, { name });

        TransformComponent transform;
        transform.position = position;
        transform.UpdateMatrix();
        m_Registry.AddComponent<TransformComponent>(entity, transform);
    }

    LightComponent light;
    light.color = color;
    light.intensity = intensity;
    light.type = type;
    light.flickerPhase = static_cast<float>(entity) * 0.137f;
    m_Registry.AddComponent<LightComponent>(entity, light);

    m_LightEntities.push_back(entity);

    return entity;
}

Entity Scene::CreateSpawnerEntity(const std::string& name, const glm::vec3& position) {
    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    m_Registry.AddComponent<NameComponent>(entity, { name });

    TransformComponent transform;
    transform.position = position;
    transform.UpdateMatrix();
    m_Registry.AddComponent<TransformComponent>(entity, transform);

    return entity;
}

void Scene::SetObjectCollision(const std::string& name, bool enabled) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<ColliderComponent>(e)) {
        m_Registry.GetComponent<ColliderComponent>(e).hasCollision = enabled;
    }
}

void Scene::SetObjectCollider(const std::string& name, int type, float radius, const glm::vec3& normal) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES) {
        if (!m_Registry.HasComponent<ColliderComponent>(e)) {
        m_Registry.AddComponent<ColliderComponent>(e, ColliderComponent{});
        }
        auto& col = m_Registry.GetComponent<ColliderComponent>(e);
        col.type = type;
        col.radius = radius;
        col.normal = glm::normalize(normal);
    }
}

void Scene::InvalidateEnvironmentEntity(Entity e) {
    if (e == m_EnvironmentEntity && !m_Registry.HasComponent<EnvironmentComponent>(e)) {
        m_EnvironmentEntity = MAX_ENTITIES;
    }
}

void Scene::SetObjectCollisionSize(const std::string& name, float radius, float height) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<ColliderComponent>(e)) {
        auto& collider = m_Registry.GetComponent<ColliderComponent>(e);
        collider.radius = radius;
        collider.height = height;
    }
}

void Scene::SetObjectTexture(const std::string& name, const std::string& texturePath) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<RenderComponent>(e)) {
        auto& renderComp = m_Registry.GetComponent<RenderComponent>(e);
        renderComp.texturePath = texturePath;
        renderComp.originalTexturePath = texturePath;
    }
}

void Scene::SetupParticleSystem(VkCommandPool commandPoolArg, VkQueue graphicsQueueArg,
    GraphicsPipeline* additivePipeline, GraphicsPipeline* alphaPipeline,
    VkDescriptorSetLayout layout, uint32_t framesInFlightArg) {
    this->commandPool = commandPoolArg;
    this->graphicsQueue = graphicsQueueArg;
    this->particlePipelineAdditive = additivePipeline;
    this->particlePipelineAlpha = alphaPipeline;
    this->particleDescriptorLayout = layout;
    this->framesInFlight = framesInFlightArg;

    for (const auto& sys : particleSystems) {
        if (sys->IsAdditive()) {
            sys->SetPipeline(particlePipelineAdditive);
        }
        else {
            sys->SetPipeline(particlePipelineAlpha);
        }
    }
}

ParticleSystem* Scene::GetOrCreateSystem(const ParticleProps& props) {
    for (const auto& sys : particleSystems) {
        if (sys->GetTexturePath() == props.texturePath) {
            return sys.get();
        }
    }

    auto newSys = std::make_unique<ParticleSystem>(device, physicalDevice, commandPool, graphicsQueue, 10000, framesInFlight);
    GraphicsPipeline* const pipeline = props.isAdditive ? particlePipelineAdditive : particlePipelineAlpha;
    newSys->Initialize(particleDescriptorLayout, pipeline, props.texturePath, props.isAdditive);

    ParticleSystem* const ptr = newSys.get();
    particleSystems.push_back(std::move(newSys));
    return ptr;
}

void Scene::AddCampfire(const std::string& name, const glm::vec3& position, float scale) {
    AddFire(position, scale);
    glm::vec3 smokePos = position;
    smokePos.y += 1.5f * scale;
    AddSmoke(smokePos, scale);
    glm::vec3 lightPos = position;
    lightPos.y += 0.5f * scale;
    const glm::vec3 lightColor = glm::vec3(1.0f, 0.5f, 0.1f);
    const float intensity = 1.0f * scale;
    AddLight(name + "_Light", lightPos, lightColor, intensity, 1);
}

int Scene::AddFire(const glm::vec3& position, float scale) {
    ParticleProps fire = ParticleLibrary::GetFireProps();
    fire.position = position;
    fire.sizeBegin *= scale;
    fire.sizeEnd *= scale;
    return GetOrCreateSystem(fire)->AddEmitter(fire, 300.0f);
}

int Scene::AddSmoke(const glm::vec3& position, float scale) {
    ParticleProps smoke = ParticleLibrary::GetSmokeProps();
    smoke.position = position;
    smoke.sizeBegin *= scale;
    smoke.sizeEnd *= scale;
    return GetOrCreateSystem(smoke)->AddEmitter(smoke, 100.0f);
}

void Scene::Ignite(Entity e) {
    if (!m_Registry.HasComponent<ThermoComponent>(e) || !m_Registry.HasComponent<TransformComponent>(e)) return;

    auto& thermo = m_Registry.GetComponent<ThermoComponent>(e);
    if (!thermo.isFlammable) return;

    if (thermo.state == ObjectState::BURNING || thermo.state == ObjectState::BURNT || thermo.state == ObjectState::REGROWING) return;

    thermo.state = ObjectState::BURNING;
    thermo.burnTimer = 0.0f;
    thermo.currentTemp = thermo.ignitionThreshold + 50.0f;

    auto& transform = m_Registry.GetComponent<TransformComponent>(e);
    const glm::vec3 pos = glm::vec3(transform.matrix[3]);

    if (thermo.fireEmitterId == -1) thermo.fireEmitterId = AddFire(pos, 0.1f);
    if (thermo.smokeEmitterId == -1) thermo.smokeEmitterId = AddSmoke(pos, 0.1f);
}

void Scene::ToggleWeather() {
    if (m_EnvironmentEntity == MAX_ENTITIES) return;
    auto& env = m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity);

    env.isPrecipitating = !env.isPrecipitating;
    env.weatherTimer = 0.0f;

    if (env.isPrecipitating) {
        if (env.currentSeason == Season::WINTER) AddSnow();
        else AddRain();
        std::cout << "Weather Toggled: Precipitation ON" << std::endl;
    }
    else {
        StopPrecipitation();
        std::cout << "Weather Toggled: Clear Skies" << std::endl;
    }
}

void Scene::AddRain() {
    if (m_RainEmitterId != -1) return;

    ParticleProps rain = ParticleLibrary::GetRainProps();
    rain.position = glm::vec3(0.0f, -50.0f, 0.0f);
    rain.positionVariation = glm::vec3(60.0f, 0.0f, 60.0f);
    rain.velocityVariation = glm::vec3(1.0f, 2.0f, 1.0f);

    auto* const sys = GetOrCreateSystem(rain);
    sys->SetSimulationBounds(glm::vec3(0.0f), 150.0f);
    m_RainEmitterId = sys->AddEmitter(rain, 4000.0f);
}

void Scene::AddSnow() {
    if (m_SnowEmitterId != -1) return;

    ParticleProps snow = ParticleLibrary::GetSnowProps();
    snow.position = glm::vec3(0.0f, -50.0f, 0.0f);
    snow.positionVariation = glm::vec3(100.0f, 0.0f, 100.0f);
    snow.velocityVariation = glm::vec3(1.0f, 0.2f, 1.0f);

    auto* const sys = GetOrCreateSystem(snow);
    sys->SetSimulationBounds(glm::vec3(0.0f), 150.0f);
    m_SnowEmitterId = sys->AddEmitter(snow, 750.0f);
}

void Scene::StopPrecipitation() {
    if (m_RainEmitterId != -1) {
        GetOrCreateSystem(ParticleLibrary::GetRainProps())->StopEmitter(m_RainEmitterId);
        m_RainEmitterId = -1;
    }
    if (m_SnowEmitterId != -1) {
        GetOrCreateSystem(ParticleLibrary::GetSnowProps())->StopEmitter(m_SnowEmitterId);
        m_SnowEmitterId = -1;
    }
}

void Scene::AddDust() {
    ParticleProps dust = ParticleLibrary::GetDustProps();
    dust.position = glm::vec3(0.0f, 5.0f, 0.0f);
    dust.velocityVariation.x = 80.0f;
    dust.velocityVariation.z = 80.0f;
    dust.velocityVariation.y = 10.0f;

    auto* const sys = GetOrCreateSystem(dust);
    sys->SetSimulationBounds(glm::vec3(0.0f), 150.0f);
    sys->AddEmitter(dust, 200.0f);
}

Entity Scene::CreateDustCloud(const std::string& name, const glm::vec3& position, const glm::vec3& direction, float speed, bool isActive) {
    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    m_Registry.AddComponent<NameComponent>(entity, { name });

    DustCloudComponent dust;
    dust.isActive = isActive;
    dust.position = position;
    dust.direction = glm::length(direction) > 0.1f ? glm::normalize(direction) : glm::vec3(1, 0, 0);
    dust.speed = speed;
    m_Registry.AddComponent<DustCloudComponent>(entity, dust);

    return entity;
}

void Scene::SpawnDustCloud() {
    Entity dustEnt = MAX_ENTITIES;
    for (Entity e = 0; e < m_Registry.GetEntityCount(); ++e) {
        if (m_Registry.HasComponent<DustCloudComponent>(e)) {
            dustEnt = e;
            break;
        }
    }

    if (dustEnt == MAX_ENTITIES) return;

    auto& dust = m_Registry.GetComponent<DustCloudComponent>(dustEnt);
    if (dust.isActive) return;

    std::cout << "Spawning Dust Cloud!" << std::endl;
    dust.isActive = true;

    if (glm::length(dust.direction) < 0.1f) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());

        const float angle = distAngle(gen);
        dust.direction = glm::vec3(cos(angle), 0.0f, sin(angle));
    }
    else {
        dust.direction = glm::normalize(dust.direction);
    }

    ParticleProps dustProps = ParticleLibrary::GetDustStormProps();
    dustProps.position = dust.position;

    auto* const sys = GetOrCreateSystem(dustProps);
    sys->SetSimulationBounds(glm::vec3(0.0f), 180.0f);
    dust.emitterId = sys->AddEmitter(dustProps, 750.0f);
}

// Update StopDust to dynamically find the component (Replaces existing logic):
void Scene::StopDust() {
    for (Entity e = 0; e < m_Registry.GetEntityCount(); ++e) {
        if (m_Registry.HasComponent<DustCloudComponent>(e)) {
            auto& dust = m_Registry.GetComponent<DustCloudComponent>(e);

            if (dust.isActive && dust.emitterId != -1) {
                GetOrCreateSystem(ParticleLibrary::GetDustStormProps())->StopEmitter(dust.emitterId);
                dust.emitterId = -1;
                dust.isActive = false;

                if (m_EnvironmentEntity != MAX_ENTITIES) {
                    auto& env = m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity);
                    env.timeSinceLastRain = 0.0f;
                }
            }
        }
    }
}

void Scene::SetObjectOrbit(const std::string& name, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && !m_Registry.HasComponent<OrbitComponent>(e)) {
        m_Registry.AddComponent<OrbitComponent>(e, OrbitComponent{});
    }

    if (e != MAX_ENTITIES && m_Registry.HasComponent<OrbitComponent>(e) && m_Registry.HasComponent<TransformComponent>(e)) {
        auto& orbit = m_Registry.GetComponent<OrbitComponent>(e);
        auto& transform = m_Registry.GetComponent<TransformComponent>(e);

        orbit.isOrbiting = true;
        orbit.center = center;
        orbit.radius = radius;
        orbit.speed = speedRadPerSec;
        orbit.axis = (glm::length(axis) > 1e-6f) ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        orbit.startVector = (glm::length(startVector) > 1e-6f) ? (glm::normalize(startVector) * radius) : glm::vec3(radius, 0.0f, 0.0f);
        orbit.initialAngle = initialAngleRad;
        orbit.currentAngle = initialAngleRad;

        const glm::quat rotation = glm::angleAxis(orbit.initialAngle, orbit.axis);
        transform.position = orbit.center + (rotation * orbit.startVector);
        transform.UpdateMatrix();
    }
}

void Scene::SetLightOrbit(const std::string& name, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad) {
    SetObjectOrbit(name, center, radius, speedRadPerSec, axis, startVector, initialAngleRad);
}

void Scene::SetOrbitSpeed(const std::string& name, float speedRadPerSec) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<OrbitComponent>(e)) {
        m_Registry.GetComponent<OrbitComponent>(e).speed = speedRadPerSec;
    }
}

void Scene::SetTimeConfig(const TimeConfig& config) {
    if (m_EnvironmentEntity != MAX_ENTITIES)
        m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).timeConfig = config;
}

void Scene::SetWeatherConfig(const WeatherConfig& config) {
    if (m_EnvironmentEntity != MAX_ENTITIES) {
        auto& env = m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity);
        env.weatherConfig = config;

        WeatherSystem ws;
        ws.PickNextWeatherDuration(env);
    }
}

void Scene::SetSeasonConfig(const SeasonConfig& config) {
    if (m_EnvironmentEntity != MAX_ENTITIES)
        m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).seasonConfig = config;
}

void Scene::SetSunHeatBonus(float bonus) {
    if (m_EnvironmentEntity != MAX_ENTITIES)
        m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).sunHeatBonus = bonus;
}

void Scene::NextSeason() {
    if (m_EnvironmentEntity == MAX_ENTITIES) return;
    auto& env = m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity);

    env.seasonTimer = 0.0f;
    env.currentSeason = static_cast<Season>((static_cast<int>(env.currentSeason) + 1) % 4);

    if (env.isPrecipitating) {
        StopPrecipitation();
        if (env.currentSeason == Season::WINTER) AddSnow();
        else AddRain();
    }
    std::cout << "Manual Season Change: " << GetSeasonName() << std::endl;
}

void Scene::ClearProceduralRegistry() {
    proceduralRegistry.clear();
}

void Scene::SetSpringVisualizationEnabled(bool enabled) {
    m_ShowSpringVisuals = enabled;
    if (!m_ShowSpringVisuals) {
        ClearSpringVisuals();
    }
}

glm::vec4 Scene::ComputeSpringVisualColor(float currentLength, float restLength) const {
    const float safeRest = std::max(restLength, 0.001f);
    const float stretch = std::abs(currentLength - safeRest) / safeRest;
    const float t = glm::clamp(stretch, 0.0f, 1.0f);
    const glm::vec3 relaxed(0.1f, 1.0f, 0.1f);
    const glm::vec3 tense(1.0f, 0.15f, 0.1f);
    return glm::vec4(glm::mix(relaxed, tense, t), 1.0f);
}

Entity Scene::GetOrCreateSpringVisualEntity(const std::string& key) {
    auto it = m_SpringVisualEntities.find(key);
    if (it != m_SpringVisualEntities.end()) {
        const Entity existing = it->second;
        if (existing < m_Registry.GetEntityCount() &&
            m_Registry.HasComponent<TransformComponent>(existing) &&
            m_Registry.HasComponent<RenderComponent>(existing)) {
            return existing;
        }
    }

    Entity visualEntity = m_Registry.CreateEntity();

    TransformComponent transform;
    transform.UpdateMatrix();
    m_Registry.AddComponent<TransformComponent>(visualEntity, transform);

    RenderComponent render;
    render.geometry = m_SpringVisualGeometry;
    render.geometryName = "spring_visual";
    render.texturePath = "";
    render.originalTexturePath = "";
    render.shadingMode = 1;
    render.castsShadow = false;
    render.originalCastsShadow = false;
    render.receiveShadows = false;
    render.layerMask = SceneLayers::ALL;
    render.onlyInRegionMask = 0;
    render.useDebugOverlay = true;
    render.debugOverlayColor = glm::vec4(0.1f, 1.0f, 0.1f, 1.0f);
    m_Registry.AddComponent<RenderComponent>(visualEntity, render);

    m_RenderableEntities.push_back(visualEntity);
    m_SpringVisualEntities[key] = visualEntity;
    return visualEntity;
}

void Scene::ClearSpringVisuals() {
    for (const auto& [_, entity] : m_SpringVisualEntities) {
        if (entity < m_Registry.GetEntityCount()) {
            DeleteEntity(entity);
        }
    }
    m_SpringVisualEntities.clear();
}

Entity Scene::GetOrCreatePathVisualEntity(const std::string& key) {
    auto it = m_PathVisualEntities.find(key);
    if (it != m_PathVisualEntities.end()) {
        const Entity existing = it->second;
        if (existing < m_Registry.GetEntityCount() &&
            m_Registry.HasComponent<TransformComponent>(existing) &&
            m_Registry.HasComponent<RenderComponent>(existing)) {
            return existing;
        }
    }

    Entity visualEntity = m_Registry.CreateEntity();

    TransformComponent transform;
    transform.UpdateMatrix();
    m_Registry.AddComponent<TransformComponent>(visualEntity, transform);

    RenderComponent render;
    render.geometry = m_SpringVisualGeometry;
    render.geometryName = "path_visual";
    render.texturePath = "";
    render.originalTexturePath = "";
    render.shadingMode = 1;
    render.castsShadow = false;
    render.originalCastsShadow = false;
    render.receiveShadows = false;
    render.layerMask = SceneLayers::ALL;
    render.onlyInRegionMask = 0;
    render.useDebugOverlay = true;
    render.debugOverlayColor = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
    m_Registry.AddComponent<RenderComponent>(visualEntity, render);

    m_RenderableEntities.push_back(visualEntity);
    m_PathVisualEntities[key] = visualEntity;
    return visualEntity;
}

void Scene::ClearPathVisuals() {
    for (const auto& [_, entity] : m_PathVisualEntities) {
        if (entity < m_Registry.GetEntityCount()) {
            DeleteEntity(entity);
        }
    }
    m_PathVisualEntities.clear();
}

void Scene::UpdatePathVisuals() {
    std::unordered_set<std::string> activeKeys;
    auto& registry = m_Registry;
    const Entity entityCount = registry.GetEntityCount();
    constexpr float kLineThickness = 0.02f;
    constexpr float kMarkerSize = 0.08f;

    auto updateLineVisual = [&](const std::string& key, const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) {
        const glm::vec3 delta = end - start;
        const float distance = glm::length(delta);
        if (distance <= 0.0001f) return;

        const glm::vec3 direction = delta / distance;
        const glm::vec3 midpoint = start + (delta * 0.5f);
        const float yaw = std::atan2(direction.x, direction.z);
        const float pitch = -std::asin(glm::clamp(direction.y, -1.0f, 1.0f));

        Entity visualEntity = GetOrCreatePathVisualEntity(key);
        auto& transform = registry.GetComponent<TransformComponent>(visualEntity);
        transform.position = midpoint;
        transform.rotation = glm::degrees(glm::vec3(pitch, yaw, 0.0f));
        transform.scale = glm::vec3(kLineThickness, kLineThickness, distance);
        transform.UpdateMatrix();

        auto& render = registry.GetComponent<RenderComponent>(visualEntity);
        render.visible = true;
        render.useDebugOverlay = true;
        render.debugOverlayColor = color;

        activeKeys.insert(key);
        };

    auto updateMarkerVisual = [&](const std::string& key, const glm::vec3& position, const glm::vec4& color) {
        Entity visualEntity = GetOrCreatePathVisualEntity(key);
        auto& transform = registry.GetComponent<TransformComponent>(visualEntity);
        transform.position = position;
        transform.rotation = glm::vec3(0.0f);
        transform.scale = glm::vec3(kMarkerSize);
        transform.UpdateMatrix();

        auto& render = registry.GetComponent<RenderComponent>(visualEntity);
        render.visible = true;
        render.useDebugOverlay = true;
        render.debugOverlayColor = color;

        activeKeys.insert(key);
        };

    for (Entity e = 0; e < entityCount; ++e) {
        if (!registry.HasComponent<PathAnimationComponent>(e)) continue;

        const auto& path = registry.GetComponent<PathAnimationComponent>(e);
        if (!path.showPath || path.segments.empty()) continue;

        for (size_t i = 0; i < path.segments.size(); ++i) {
            const auto& segment = path.segments[i];
            const std::string base = "PathVis_" + std::to_string(e) + "_" + std::to_string(i);

            if (segment.curveType == PathCurveType::BezierQuadratic) {
                constexpr int kSamples = 20;
                glm::vec3 previous = AnimationMath::QuadraticBezier(segment.startPoint, segment.controlPoint, segment.endPoint, 0.0f);
                for (int s = 1; s <= kSamples; ++s) {
                    const float t = static_cast<float>(s) / static_cast<float>(kSamples);
                    const glm::vec3 current = AnimationMath::QuadraticBezier(segment.startPoint, segment.controlPoint, segment.endPoint, t);
                    updateLineVisual(base + "_curve_" + std::to_string(s), previous, current, path.pathColor);
                    previous = current;
                }
                updateMarkerVisual(base + "_ctrl", segment.controlPoint, glm::vec4(1.0f, 1.0f, 0.1f, 1.0f));
            }
            else {
                updateLineVisual(base + "_line", segment.startPoint, segment.endPoint, path.pathColor);
            }

            updateMarkerVisual(base + "_start", segment.startPoint, glm::vec4(0.1f, 1.0f, 0.1f, 1.0f));
            updateMarkerVisual(base + "_end", segment.endPoint, glm::vec4(0.1f, 1.0f, 0.1f, 1.0f));
        }
    }

    for (auto it = m_PathVisualEntities.begin(); it != m_PathVisualEntities.end(); ) {
        if (activeKeys.find(it->first) == activeKeys.end()) {
            DeleteEntity(it->second);
            it = m_PathVisualEntities.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Scene::UpdateSpringVisuals() {
    std::unordered_set<std::string> activeKeys;
    auto& registry = m_Registry;
    const Entity entityCount = registry.GetEntityCount();
    constexpr float kThickness = 0.03f;

    auto updateVisual = [&](const std::string& key, const glm::vec3& posA, const glm::vec3& posB, float restLength) {
        const glm::vec3 delta = posB - posA;
        const float distance = glm::length(delta);
        if (distance <= 0.0001f) {
            return;
        }

        const glm::vec3 direction = delta / distance;
        const glm::vec3 midpoint = posA + (delta * 0.5f);

        const float yaw = std::atan2(direction.x, direction.z);
        const float pitch = -std::asin(glm::clamp(direction.y, -1.0f, 1.0f));
        const glm::vec3 eulerDegrees = glm::degrees(glm::vec3(pitch, yaw, 0.0f));

        Entity visualEntity = GetOrCreateSpringVisualEntity(key);
        auto& transform = registry.GetComponent<TransformComponent>(visualEntity);
        transform.position = midpoint;
        transform.rotation = eulerDegrees;
        transform.scale = glm::vec3(kThickness, kThickness, distance);
        transform.UpdateMatrix();

        auto& render = registry.GetComponent<RenderComponent>(visualEntity);
        render.visible = true;
        render.useDebugOverlay = true;
        render.debugOverlayColor = ComputeSpringVisualColor(distance, restLength);

        activeKeys.insert(key);
    };

    for (Entity e = 0; e < entityCount; ++e) {
        if (!registry.HasComponent<SpringComponent>(e) || !registry.HasComponent<TransformComponent>(e)) {
            continue;
        }

        auto& spring = registry.GetComponent<SpringComponent>(e);
        const glm::vec3 posA = registry.GetComponent<TransformComponent>(e).position;

        if (!spring.isAttachedToEntity) {
            const std::string key = "SpringVis_" + std::to_string(e) + "_Fixed";
            updateVisual(key, posA, spring.fixedAnchorPoint, spring.restingLength);
            continue;
        }

        for (size_t i = 0; i < spring.connectedEntities.size(); ++i) {
            const Entity target = spring.connectedEntities[i];
            if (target == MAX_ENTITIES || target >= entityCount) continue;
            if (!registry.HasComponent<TransformComponent>(target)) continue;

            const glm::vec3 posB = registry.GetComponent<TransformComponent>(target).position;
            const std::string key = "SpringVis_" + std::to_string(e) + "_" + std::to_string(i);
            updateVisual(key, posA, posB, spring.restingLength);
        }
    }

    for (auto it = m_SpringVisualEntities.begin(); it != m_SpringVisualEntities.end(); ) {
        if (activeKeys.find(it->first) == activeKeys.end()) {
            DeleteEntity(it->second);
            it = m_SpringVisualEntities.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Scene::Update(float deltaTime) {
    m_ElapsedTime += std::max(0.0f, deltaTime);
    for (auto& sys : m_Systems) {
        sys->Update(*this, deltaTime);
    }

    if (m_ShowSpringVisuals) {
        UpdateSpringVisuals();
    }
    else if (!m_SpringVisualEntities.empty()) {
        ClearSpringVisuals();
    }

    UpdatePathVisuals();
}

std::vector<Light> Scene::GetLights() const {
    std::vector<Light> lights;
    lights.reserve(m_LightEntities.size());

    for (Entity e : m_LightEntities) {
        if (m_Registry.HasComponent<LightComponent>(e) && m_Registry.HasComponent<TransformComponent>(e)) {
            auto& lightComp = m_Registry.GetComponent<LightComponent>(e);
            auto& transComp = m_Registry.GetComponent<TransformComponent>(e);

            Light vLight{};
            vLight.position = glm::vec3(transComp.matrix[3]);
            vLight.color = lightComp.color;
            float finalIntensity = lightComp.intensity;
            if (lightComp.flickerEnabled && lightComp.flickerAmount > 0.001f) {
                const float pattern = ComputeFlickerPresetValue(lightComp.flickerPreset, m_ElapsedTime, lightComp.flickerPhase);
                const float amount = glm::clamp(lightComp.flickerAmount, 0.0f, 1.0f);
                finalIntensity *= (1.0f - amount) + (amount * pattern);
            }
            vLight.intensity = finalIntensity;
            vLight.type = lightComp.type;
            vLight.layerMask = lightComp.layerMask;

            // --- NEW: Map Spotlight variables ---
            vLight.direction = lightComp.direction;
            // Precalculate cosine of the angle here for GPU performance!
            vLight.cutoffAngle = glm::cos(glm::radians(lightComp.cutoffAngle));

            lights.push_back(vLight);
        }
    }
    return lights;
}

void Scene::Clear() {
    for (Entity e : m_RenderableEntities) {
        if (m_Registry.HasComponent<RenderComponent>(e)) {
            auto& renderComp = m_Registry.GetComponent<RenderComponent>(e);
            if (renderComp.geometry) {
                if (renderComp.geometry.use_count() == 1) {
                    m_DeferredGeometryCleanup.push_back(std::move(renderComp.geometry));
                }
            }
        }
    }

    for (Entity i = 0; i < m_Registry.GetEntityCount(); i++) {
        m_Registry.DestroyEntity(i);
    }

    m_EntityMap.clear();
    m_RenderableEntities.clear();
    m_LightEntities.clear();
    particleSystems.clear();
    m_SpringVisualEntities.clear();
    m_PathVisualEntities.clear();

    FlushDeferredGeometryCleanup();

    //// 1. Recreate Environment Entity
    //m_EnvironmentEntity = m_Registry.CreateEntity();
    //m_Registry.AddComponent<NameComponent>(m_EnvironmentEntity, { "GlobalEnvironment" });
    //m_Registry.AddComponent<EnvironmentComponent>(m_EnvironmentEntity, EnvironmentComponent{});
    //m_EntityMap["GlobalEnvironment"] = m_EnvironmentEntity;

    //// 2. Recreate Dust Cloud Entity
    //Entity dustEntity = m_Registry.CreateEntity();
    //m_Registry.AddComponent<NameComponent>(dustEntity, { "GlobalDustCloud" });
    //m_Registry.AddComponent<DustCloudComponent>(dustEntity, DustCloudComponent{});
    //m_EntityMap["GlobalDustCloud"] = dustEntity;

    m_EnvironmentEntity = MAX_ENTITIES;
}

void Scene::FlushDeferredGeometryCleanup() {
    if (m_DeferredGeometryCleanup.empty()) {
        return;
    }

    vkDeviceWaitIdle(device);
    for (auto& geometry : m_DeferredGeometryCleanup) {
        if (geometry) {
            geometry->Cleanup();
        }
    }
    m_DeferredGeometryCleanup.clear();
}

void Scene::SetObjectTransform(const std::string& name, const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<TransformComponent>(e)) {
        auto& comp = m_Registry.GetComponent<TransformComponent>(e);
        comp.position = pos;
        comp.rotation = rot;
        comp.scale = scale;
        comp.UpdateMatrix();
    }
}

void Scene::SetObjectRegionVisibilityMasks(const std::string& name, int onlyInRegionMask) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<RenderComponent>(e)) {
        m_Registry.GetComponent<RenderComponent>(e).onlyInRegionMask = onlyInRegionMask;
    }
}

void Scene::SetObjectLayerMask(const std::string& name, int mask) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<RenderComponent>(e)) {
        m_Registry.GetComponent<RenderComponent>(e).layerMask = mask;
    }
}

void Scene::SetLightLayerMask(const std::string& name, int mask) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<LightComponent>(e)) {
        m_Registry.GetComponent<LightComponent>(e).layerMask = mask;
    }
}

void Scene::SetLightFlicker(const std::string& name, bool enabled, float amount, int preset) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<LightComponent>(e)) {
        auto& light = m_Registry.GetComponent<LightComponent>(e);
        light.flickerEnabled = enabled;
        light.flickerAmount = glm::clamp(amount, 0.0f, 1.0f);
        light.flickerPreset = glm::clamp(preset, 0, 4);
    }
}

void Scene::SetObjectVisible(const std::string& name, bool visible) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<RenderComponent>(e)) {
        m_Registry.GetComponent<RenderComponent>(e).visible = visible;
    }
}

void Scene::SetObjectCastsShadow(const std::string& name, bool casts) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<RenderComponent>(e)) {
        auto& render = m_Registry.GetComponent<RenderComponent>(e);
        render.castsShadow = casts;
        render.originalCastsShadow = casts;
    }
}

void Scene::SetObjectReceivesShadows(const std::string& name, bool receives) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<RenderComponent>(e)) {
        m_Registry.GetComponent<RenderComponent>(e).receiveShadows = receives;
    }
}

void Scene::SetObjectShadingMode(const std::string& name, int mode) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<RenderComponent>(e)) {
        m_Registry.GetComponent<RenderComponent>(e).shadingMode = mode;
    }
}

void Scene::StopObjectFire(Entity e) {
    if (!m_Registry.HasComponent<ThermoComponent>(e)) return;

    auto& thermo = m_Registry.GetComponent<ThermoComponent>(e);

    if (thermo.fireEmitterId != -1) {
        GetOrCreateSystem(ParticleLibrary::GetFireProps())->StopEmitter(thermo.fireEmitterId);
        thermo.fireEmitterId = -1;
    }
    if (thermo.smokeEmitterId != -1) {
        GetOrCreateSystem(ParticleLibrary::GetSmokeProps())->StopEmitter(thermo.smokeEmitterId);
        thermo.smokeEmitterId = -1;
    }

    if (thermo.fireLightEntity != -1 && thermo.fireLightEntity != MAX_ENTITIES) {
        if (m_Registry.HasComponent<LightComponent>(thermo.fireLightEntity)) {
            m_Registry.GetComponent<LightComponent>(thermo.fireLightEntity).intensity = 0.0f;
        }
    }
}

std::string Scene::GetSeasonName() const {
    if (m_EnvironmentEntity == MAX_ENTITIES) return "Unknown";
    switch (m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).currentSeason) {
    case Season::SUMMER: return "Summer";
    case Season::AUTUMN: return "Autumn";
    case Season::WINTER: return "Winter";
    case Season::SPRING: return "Spring";
    }
    return "Unknown";
}

float Scene::GetWeatherIntensity() const {
    if (m_EnvironmentEntity != MAX_ENTITIES)
        return m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).weatherIntensity;
    return 0.0f;
}

bool Scene::IsPrecipitating() const {
    if (m_EnvironmentEntity != MAX_ENTITIES)
        return m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).isPrecipitating;
    return false;
}

float Scene::GetSunHeatBonus() const {
    if (m_EnvironmentEntity != MAX_ENTITIES)
        return m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).sunHeatBonus;
    return 0.0f;
}

float Scene::GetPostRainFireSuppressionTimer() const {
    if (m_EnvironmentEntity != MAX_ENTITIES)
        return m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).postRainFireSuppressionTimer;
    return 0.0f;
}

const TimeConfig& Scene::GetTimeConfig() const {
    static TimeConfig empty;
    if (m_EnvironmentEntity != MAX_ENTITIES)
        return m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity).timeConfig;
    return empty;
}

bool Scene::IsDustActive() const {
    Entity e = GetEntityByName("GlobalDustCloud");
    if (e != MAX_ENTITIES)
        return m_Registry.GetComponent<DustCloudComponent>(e).isActive;
    return false;
}

void Scene::ResetEnvironment() {
    for (Entity e = 0; e < m_Registry.GetEntityCount(); ++e) {
        if (m_Registry.HasComponent<OrbitComponent>(e) && m_Registry.HasComponent<TransformComponent>(e)) {
            auto& orbit = m_Registry.GetComponent<OrbitComponent>(e);
            auto& transform = m_Registry.GetComponent<TransformComponent>(e);

            if (orbit.isOrbiting) {
                orbit.currentAngle = orbit.initialAngle;
                const glm::quat rotation = glm::angleAxis(orbit.currentAngle, orbit.axis);
                const glm::vec3 offset = rotation * orbit.startVector;
                transform.position = orbit.center + offset;
                transform.UpdateMatrix();
            }
        }

        if (m_Registry.HasComponent<ThermoComponent>(e) && m_Registry.HasComponent<RenderComponent>(e) && m_Registry.HasComponent<TransformComponent>(e)) {
            auto& thermo = m_Registry.GetComponent<ThermoComponent>(e);
            auto& render = m_Registry.GetComponent<RenderComponent>(e);
            auto& transform = m_Registry.GetComponent<TransformComponent>(e);

            if (thermo.isFlammable) {
                StopObjectFire(e);

                if (thermo.storedOriginalGeometry || thermo.state == ObjectState::REGROWING) {
                    if (thermo.storedOriginalGeometry) {
                        render.geometry = thermo.storedOriginalGeometry;
                        thermo.storedOriginalGeometry = nullptr;
                    }

                    transform.position = thermo.storedOriginalPosition;
                    transform.rotation = thermo.storedOriginalRotation;
                    transform.scale = thermo.storedOriginalScale;
                    transform.UpdateMatrix();
                }

                render.texturePath = render.originalTexturePath;
                thermo.state = ObjectState::NORMAL;
                thermo.currentTemp = 0.0f;
                thermo.burnTimer = 0.0f;
                thermo.regrowTimer = 0.0f;
                thermo.burnFactor = 0.0f;
            }
        }
    }

    StopPrecipitation();
    StopDust();

    if (m_EnvironmentEntity != MAX_ENTITIES) {
        auto& env = m_Registry.GetComponent<EnvironmentComponent>(m_EnvironmentEntity);
        env.isPrecipitating = false;
        env.weatherTimer = 0.0f;
        env.timeSinceLastRain = 0.0f;
    }
}

Entity Scene::CreateCameraEntity(const std::string& name, const glm::vec3& pos, const std::string& type) {
    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    m_Registry.AddComponent<NameComponent>(entity, { name });

    TransformComponent transform;
    transform.position = pos;
    transform.UpdateMatrix();
    m_Registry.AddComponent<TransformComponent>(entity, transform);

    CameraComponent camera;
    m_Registry.AddComponent<CameraComponent>(entity, camera);

    // If it's an Orbit or RandomTarget camera, add the OrbitComponent
    if (type == "Orbit" || type == "RandomTarget") {
        OrbitComponent orbit;
        orbit.isOrbiting = true;
        m_Registry.AddComponent<OrbitComponent>(entity, orbit);
    }

    return entity;
}