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
#include "Camera.h"

// ECS Systems
#include "../systems/OrbitSystem.h"
#include "../systems/SimpleShadowSystem.h"
#include "../systems/ThermodynamicsSystem.h"
#include "../systems/TimeSystem.h"
#include "../systems/WeatherSystem.h"
#include "../systems/ParticleUpdateSystem.h"
#include "../systems/CameraSystem.h"


Entity Scene::GetEntityByName(const std::string& name) const {
    auto it = m_EntityMap.find(name);
    if (it != m_EntityMap.end()) {
        return it->second;
    }
    return MAX_ENTITIES;
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
    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    m_Registry.AddComponent<NameComponent>(entity, { name });

    TransformComponent transform;
    transform.matrix = glm::translate(glm::mat4(1.0f), position);
    m_Registry.AddComponent<TransformComponent>(entity, transform);

    RenderComponent render;
    render.geometry = geometry;
    render.texturePath = texturePath;
    render.originalTexturePath = texturePath;
    render.shadingMode = globalShadingMode;
    m_Registry.AddComponent<RenderComponent>(entity, render);

    m_RenderableEntities.push_back(entity);

    ThermoComponent thermo;
    thermo.isFlammable = isFlammable;
    m_Registry.AddComponent<ThermoComponent>(entity, thermo);

    m_Registry.AddComponent<ColliderComponent>(entity, ColliderComponent{});
    m_Registry.AddComponent<OrbitComponent>(entity, OrbitComponent{});

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

    // 1. Create Global Environment Entity
    m_EnvironmentEntity = m_Registry.CreateEntity();
    m_Registry.AddComponent<NameComponent>(m_EnvironmentEntity, { "GlobalEnvironment" });
    m_Registry.AddComponent<EnvironmentComponent>(m_EnvironmentEntity, EnvironmentComponent{});

    // 2. Create Global Dust Cloud Entity
    Entity dustEntity = m_Registry.CreateEntity();
    m_Registry.AddComponent<NameComponent>(dustEntity, { "GlobalDustCloud" });
    m_Registry.AddComponent<DustCloudComponent>(dustEntity, DustCloudComponent{});

    // 3. Register ECS Systems
    m_Systems.push_back(std::make_unique<CameraSystem>());
    m_Systems.push_back(std::make_unique<OrbitSystem>());
    m_Systems.push_back(std::make_unique<TimeSystem>());
    m_Systems.push_back(std::make_unique<WeatherSystem>());
    m_Systems.push_back(std::make_unique<ParticleUpdateSystem>());
    m_Systems.push_back(std::make_unique<SimpleShadowSystem>());
    m_Systems.push_back(std::make_unique<ThermodynamicsSystem>());
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
        int selectedIndex = 0;
        for (int k = 0; k < proceduralRegistry.size(); k++) {
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
                glm::mat4 m = glm::mat4(1.0f);
                m = glm::translate(m, glm::vec3(x, y, z));
                const float randomYaw = distRot(gen);
                m = glm::rotate(m, glm::radians(randomYaw), glm::vec3(0.0f, 1.0f, 0.0f));

                if (glm::length(config.baseRotation) > 0.001f) {
                    m = glm::rotate(m, glm::radians(config.baseRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                    m = glm::rotate(m, glm::radians(config.baseRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                    m = glm::rotate(m, glm::radians(config.baseRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                }
                m = glm::scale(m, scale);
                m_Registry.GetComponent<TransformComponent>(mainObj).matrix = m;
            }
        }
    }
}

void Scene::AddTerrain(const std::string& name, float radius, int rings, int segments, float heightScale, float noiseFreq, const glm::vec3& position, const std::string& texturePath) {
    auto geo = GeometryGenerator::CreateTerrain(device, physicalDevice, radius - 1, rings, segments, heightScale, noiseFreq);
    Entity entity = AddObjectInternal(name, std::move(geo), position, texturePath, false);

    auto& collider = m_Registry.GetComponent<ColliderComponent>(entity);
    collider.hasCollision = false;

    m_TerrainConfig.exists = true;
    m_TerrainConfig.radius = radius;
    m_TerrainConfig.heightScale = heightScale;
    m_TerrainConfig.noiseFreq = noiseFreq;
    m_TerrainConfig.position = position;
}

void Scene::AddBowl(const std::string& name, float radius, int slices, int stacks, const glm::vec3& position, const std::string& texturePath) {
    AddObjectInternal(name, GeometryGenerator::CreateBowl(device, physicalDevice, radius, slices, stacks), position, texturePath, false);
}

void Scene::AddPedestal(const std::string& name, float topRadius, float baseWidth, float height, const glm::vec3& position, const std::string& texturePath) {
    auto geo = GeometryGenerator::CreatePedestal(device, physicalDevice, topRadius, baseWidth, height, 512, 512);
    Entity entity = AddObjectInternal(name, std::move(geo), position, texturePath, false);

    auto& collider = m_Registry.GetComponent<ColliderComponent>(entity);
    collider.radius = std::max(topRadius, baseWidth);
    collider.height = height;
    collider.hasCollision = true;
}

void Scene::AddCube(const std::string& name, const glm::vec3& position, const glm::vec3& scale, const std::string& texturePath) {
    auto geo = GeometryGenerator::CreateCube(device, physicalDevice);
    Entity entity = AddObjectInternal(name, std::move(geo), position, texturePath, false);

    auto& transform = m_Registry.GetComponent<TransformComponent>(entity);
    transform.matrix = glm::scale(transform.matrix, scale);
}

void Scene::AddGrid(const std::string& name, int rows, int cols, float cellSize, const glm::vec3& position, const std::string& texturePath) {
    AddObjectInternal(name, GeometryGenerator::CreateGrid(device, physicalDevice, rows, cols, cellSize), position, texturePath, false);
}

void Scene::AddSphere(const std::string& name, int stacks, int slices, float radius, const glm::vec3& position, const std::string& texturePath) {
    AddObjectInternal(name, GeometryGenerator::CreateSphere(device, physicalDevice, stacks, slices, radius), position, texturePath, false);
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

        auto& transform = m_Registry.GetComponent<TransformComponent>(entity);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = glm::rotate(m, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        m = glm::scale(m, scale);

        transform.matrix = m;
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

    auto& shadowRender = m_Registry.GetComponent<RenderComponent>(shadowEntity);
    shadowRender.castsShadow = false;
    shadowRender.originalCastsShadow = false;
    shadowRender.receiveShadows = false;
    shadowRender.shadingMode = 0;
    // Match the parent object's visibility
    shadowRender.visible = m_Registry.GetComponent<RenderComponent>(targetEntity).visible;

    auto& shadowThermo = m_Registry.GetComponent<ThermoComponent>(shadowEntity);
    shadowThermo.burnFactor = 1.0f; // Force black color

    auto& shadowCollider = m_Registry.GetComponent<ColliderComponent>(shadowEntity);
    shadowCollider.hasCollision = false;

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
        transform.matrix = glm::translate(glm::mat4(1.0f), position);
        m_Registry.AddComponent<TransformComponent>(entity, transform);
        m_Registry.AddComponent<OrbitComponent>(entity, OrbitComponent{});
    }

    LightComponent light;
    light.color = color;
    light.intensity = intensity;
    light.type = type;
    m_Registry.AddComponent<LightComponent>(entity, light);

    m_LightEntities.push_back(entity);

    return entity;
}

void Scene::SetObjectCollision(const std::string& name, bool enabled) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<ColliderComponent>(e)) {
        m_Registry.GetComponent<ColliderComponent>(e).hasCollision = enabled;
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

void Scene::SpawnDustCloud() {
    Entity dustEnt = GetEntityByName("GlobalDustCloud");
    if (dustEnt == MAX_ENTITIES) return;

    auto& dust = m_Registry.GetComponent<DustCloudComponent>(dustEnt);
    if (dust.isActive) return;

    std::cout << "Spawning Dust Cloud!" << std::endl;

    dust.isActive = true;
    dust.position = glm::vec3(0.0f, -70.0f, 0.0f);

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());

    const float angle = distAngle(gen);
    dust.direction = glm::vec3(cos(angle), 0.0f, sin(angle));

    ParticleProps dustProps = ParticleLibrary::GetDustStormProps();
    dustProps.position = dust.position;

    auto* const sys = GetOrCreateSystem(dustProps);
    sys->SetSimulationBounds(glm::vec3(0.0f), 180.0f);
    dust.emitterId = sys->AddEmitter(dustProps, 750.0f);
}

void Scene::StopDust() {
    Entity dustEnt = GetEntityByName("GlobalDustCloud");
    if (dustEnt == MAX_ENTITIES) return;

    auto& dust = m_Registry.GetComponent<DustCloudComponent>(dustEnt);

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

void Scene::SetObjectOrbit(const std::string& name, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad) {
    Entity e = GetEntityByName(name);
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
        transform.matrix[3] = glm::vec4(orbit.center + (rotation * orbit.startVector), 1.0f);
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

void Scene::Update(float deltaTime) {
    for (auto& sys : m_Systems) {
        sys->Update(*this, deltaTime);
    }
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
            vLight.intensity = lightComp.intensity;
            vLight.type = lightComp.type;
            vLight.layerMask = lightComp.layerMask;
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
                renderComp.geometry->Cleanup();
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

    // 1. Recreate Environment Entity
    m_EnvironmentEntity = m_Registry.CreateEntity();
    m_Registry.AddComponent<NameComponent>(m_EnvironmentEntity, { "GlobalEnvironment" });
    m_Registry.AddComponent<EnvironmentComponent>(m_EnvironmentEntity, EnvironmentComponent{});
    m_EntityMap["GlobalEnvironment"] = m_EnvironmentEntity;

    // 2. Recreate Dust Cloud Entity
    Entity dustEntity = m_Registry.CreateEntity();
    m_Registry.AddComponent<NameComponent>(dustEntity, { "GlobalDustCloud" });
    m_Registry.AddComponent<DustCloudComponent>(dustEntity, DustCloudComponent{});
    m_EntityMap["GlobalDustCloud"] = dustEntity;
}

void Scene::SetObjectTransform(const std::string& name, const glm::mat4& transform) {
    Entity e = GetEntityByName(name);
    if (e != MAX_ENTITIES && m_Registry.HasComponent<TransformComponent>(e)) {
        m_Registry.GetComponent<TransformComponent>(e).matrix = transform;
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
                transform.matrix[3] = glm::vec4(orbit.center + offset, 1.0f);
            }
        }

        if (m_Registry.HasComponent<ThermoComponent>(e) && m_Registry.HasComponent<RenderComponent>(e) && m_Registry.HasComponent<TransformComponent>(e)) {
            auto& thermo = m_Registry.GetComponent<ThermoComponent>(e);
            auto& render = m_Registry.GetComponent<RenderComponent>(e);
            auto& transform = m_Registry.GetComponent<TransformComponent>(e);

            if (thermo.isFlammable) {
                StopObjectFire(e);

                if (thermo.storedOriginalGeometry) {
                    render.geometry = thermo.storedOriginalGeometry;
                    thermo.storedOriginalGeometry = nullptr;
                    transform.matrix = thermo.storedOriginalTransform;
                }
                else if (thermo.state == ObjectState::REGROWING) {
                    transform.matrix = thermo.storedOriginalTransform;
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

Entity Scene::CreateCameraEntity(const std::string& name, const glm::vec3& pos, CameraType type) {
    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    m_Registry.AddComponent<NameComponent>(entity, { name });

    TransformComponent transform;
    transform.matrix = glm::translate(glm::mat4(1.0f), pos);
    m_Registry.AddComponent<TransformComponent>(entity, transform);

    CameraComponent camera;
    m_Registry.AddComponent<CameraComponent>(entity, camera);

    // If it's an Orbit camera, we can give it an OrbitComponent right now!
    if (type == CameraType::OUTSIDE_ORB || type == CameraType::CACTI) {
        OrbitComponent orbit;
        orbit.isOrbiting = true;
        m_Registry.AddComponent<OrbitComponent>(entity, orbit);
    }

    return entity;
}