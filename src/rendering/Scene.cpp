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
#include "../systems/OrbitSystem.h"
#include "../systems/SimpleShadowSystem.h"
#include "../systems/ThermodynamicsSystem.h"

namespace {
    // Utility to avoid repeating the const_cast boilerplate when reading from the registry in const methods
    Registry& GetMutRegistry(const Registry& reg) {
        return const_cast<Registry&>(reg);
    }
}

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

    m_Systems.push_back(std::make_unique<OrbitSystem>());
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

void Scene::AddSimpleShadow(const std::string& objectName, float radius) {
    Entity targetEntity = GetEntityByName(objectName);
    if (targetEntity == MAX_ENTITIES) return;

    auto diskGeo = GeometryGenerator::CreateDisk(device, physicalDevice, radius, 16);
    std::string shadowName = objectName + "_Shadow";

    Entity shadowEntity = AddObjectInternal(shadowName, std::move(diskGeo), glm::vec3(0.0f), "textures/shadow.jpg", false);

    auto& shadowRender = m_Registry.GetComponent<RenderComponent>(shadowEntity);
    shadowRender.castsShadow = false;
    shadowRender.receiveShadows = false;
    shadowRender.shadingMode = 0;
    shadowRender.visible = false;

    auto& shadowThermo = m_Registry.GetComponent<ThermoComponent>(shadowEntity);
    shadowThermo.burnFactor = 1.0f; // Force black color

    auto& shadowCollider = m_Registry.GetComponent<ColliderComponent>(shadowEntity);
    shadowCollider.hasCollision = false;

    auto& targetRender = m_Registry.GetComponent<RenderComponent>(targetEntity);
    targetRender.simpleShadowEntity = shadowEntity;
}

void Scene::ToggleSimpleShadows() {
    m_UseSimpleShadows = !m_UseSimpleShadows;

    for (Entity e : m_RenderableEntities) {
        if (!m_Registry.HasComponent<RenderComponent>(e)) continue;

        auto& render = m_Registry.GetComponent<RenderComponent>(e);
        if (render.simpleShadowEntity != MAX_ENTITIES && m_Registry.HasComponent<RenderComponent>(render.simpleShadowEntity)) {
            auto& shadowRender = m_Registry.GetComponent<RenderComponent>(render.simpleShadowEntity);

            if (m_UseSimpleShadows) {
                render.castsShadow = false;
                shadowRender.visible = render.visible;
            }
            else {
                render.castsShadow = true;
                shadowRender.visible = false;
            }
        }
    }
    std::cout << "Shadow Mode: " << (m_UseSimpleShadows ? "Simple" : "Normal") << std::endl;
}

void Scene::UpdateSimpleShadows() {
    if (!m_UseSimpleShadows) return;

    glm::vec3 lightPos = glm::vec3(0.0f, 100.0f, 0.0f);
    bool sunIsUp = false;

    Entity sunEntity = GetEntityByName("Sun");
    if (sunEntity != MAX_ENTITIES && m_Registry.HasComponent<TransformComponent>(sunEntity)) {
        auto& sunTransform = m_Registry.GetComponent<TransformComponent>(sunEntity);
        lightPos = glm::vec3(sunTransform.matrix[3]);
        if (lightPos.y > -20.0f) sunIsUp = true;
    }

    for (Entity e : m_RenderableEntities) {
        if (!m_Registry.HasComponent<RenderComponent>(e) || !m_Registry.HasComponent<TransformComponent>(e)) continue;

        auto& render = m_Registry.GetComponent<RenderComponent>(e);
        if (render.simpleShadowEntity == MAX_ENTITIES) continue;

        auto& shadowRender = m_Registry.GetComponent<RenderComponent>(render.simpleShadowEntity);
        auto& shadowTransform = m_Registry.GetComponent<TransformComponent>(render.simpleShadowEntity);
        auto& parentTransform = m_Registry.GetComponent<TransformComponent>(e);

        if (sunIsUp && render.visible) {
            const glm::vec3 parentPos = glm::vec3(parentTransform.matrix[3]);
            const glm::vec3 rawLightDir = parentPos + glm::vec3(0.0f, 0.15f, 0.0f) - lightPos;
            const glm::vec3 lightDir3D = glm::normalize(rawLightDir);

            glm::vec3 flatDir = glm::vec3(lightDir3D.x, 0.0f, lightDir3D.z);
            flatDir = (glm::length(flatDir) > 0.001f) ? glm::normalize(flatDir) : glm::vec3(0.0f, 0.0f, 1.0f);

            const float angle = std::atan2(flatDir.x, flatDir.z);
            const float dotY = std::abs(lightDir3D.y);
            float stretch = std::clamp(1.0f + (1.0f - dotY) * 8.0f, 1.0f, 12.0f);

            const float parentScale = glm::length(glm::vec3(parentTransform.matrix[0]));
            const float shadowRadius = std::max(parentScale * 1.5f, 0.5f);
            const float shiftAmount = shadowRadius * (stretch - 1.0f);
            const glm::vec3 finalPos = parentPos + glm::vec3(0.0f, 0.15f, 0.0f) + (flatDir * shiftAmount);

            glm::mat4 m = glm::mat4(1.0f);
            m = glm::translate(m, finalPos);
            m = glm::rotate(m, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::scale(m, glm::vec3(1.0f, 1.0f, stretch));

            shadowTransform.matrix = m;
            shadowRender.visible = true;
        }
        else {
            shadowRender.visible = false;
        }
    }
}

Entity Scene::AddLight(const std::string& name, const glm::vec3& position, const glm::vec3& color, float intensity, int type) {
    if (m_LightEntities.size() >= MAX_LIGHTS) {
        std::cerr << "Warning: Maximum number of lights reached." << std::endl;
        return MAX_ENTITIES;
    }

    Entity entity = m_Registry.CreateEntity();
    m_EntityMap[name] = entity;

    m_Registry.AddComponent<NameComponent>(entity, { name });

    TransformComponent transform;
    transform.matrix = glm::translate(glm::mat4(1.0f), position);
    m_Registry.AddComponent<TransformComponent>(entity, transform);

    LightComponent light;
    light.color = color;
    light.intensity = intensity;
    light.type = type;
    m_Registry.AddComponent<LightComponent>(entity, light);

    m_Registry.AddComponent<OrbitComponent>(entity, OrbitComponent{});

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
    m_IsPrecipitating = !m_IsPrecipitating;
    m_WeatherTimer = 0.0f;
    PickNextWeatherDuration();

    if (m_IsPrecipitating) {
        if (m_CurrentSeason == Season::WINTER) AddSnow();
        else AddRain();
        std::cout << "Weather Toggled: Precipitation ON (" << m_CurrentWeatherDurationTarget << "s)" << std::endl;
    }
    else {
        StopPrecipitation();
        std::cout << "Weather Toggled: Clear Skies (" << m_CurrentWeatherDurationTarget << "s)" << std::endl;
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
    if (m_DustActive) return;

    std::cout << "Spawning Dust Cloud!" << std::endl;

    m_DustActive = true;
    m_DustPosition = glm::vec3(0.0f, -70.0f, 0.0f);

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());

    const float angle = distAngle(gen);
    m_DustDirection = glm::vec3(cos(angle), 0.0f, sin(angle));

    ParticleProps dust = ParticleLibrary::GetDustStormProps();
    dust.position = m_DustPosition;

    auto* const sys = GetOrCreateSystem(dust);
    sys->SetSimulationBounds(glm::vec3(0.0f), 180.0f);
    m_DustEmitterId = sys->AddEmitter(dust, 750.0f);
}

void Scene::StopDust() {
    if (m_DustActive && m_DustEmitterId != -1) {
        GetOrCreateSystem(ParticleLibrary::GetDustStormProps())->StopEmitter(m_DustEmitterId);
        m_DustEmitterId = -1;
        m_DustActive = false;
        m_TimeSinceLastRain = 0.0f;
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
    m_TimeConfig = config;
}

void Scene::SetWeatherConfig(const WeatherConfig& config) {
    m_WeatherConfig = config;
    PickNextWeatherDuration();
}

void Scene::SetSeasonConfig(const SeasonConfig& config) {
    m_SeasonConfig = config;
}

void Scene::PickNextWeatherDuration() {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    if (m_IsPrecipitating) {
        std::uniform_real_distribution<float> dist(m_WeatherConfig.minPrecipitationDuration, m_WeatherConfig.maxPrecipitationDuration);
        m_CurrentWeatherDurationTarget = dist(gen);
    }
    else {
        std::uniform_real_distribution<float> dist(m_WeatherConfig.minClearInterval, m_WeatherConfig.maxClearInterval);
        m_CurrentWeatherDurationTarget = dist(gen);
    }
}

void Scene::NextSeason() {
    m_SeasonTimer = 0.0f;
    m_CurrentSeason = static_cast<Season>((static_cast<int>(m_CurrentSeason) + 1) % 4);

    if (m_IsPrecipitating) {
        StopPrecipitation();
        if (m_CurrentSeason == Season::WINTER) AddSnow();
        else AddRain();
    }
    std::cout << "Manual Season Change: " << GetSeasonName() << std::endl;
}

void Scene::SetSunHeatBonus(float bonus) {
    m_SunHeatBonus = bonus;
}

void Scene::ClearProceduralRegistry() {
    proceduralRegistry.clear();
}

void Scene::Update(float deltaTime) {
    if (m_IsPrecipitating) {
        m_TimeSinceLastRain = 0.0f;
        StopDust();
        m_PostRainFireSuppressionTimer = m_WeatherConfig.fireSuppressionDuration;
    }
    else {
        m_TimeSinceLastRain += deltaTime;
        if (m_PostRainFireSuppressionTimer > 0.0f) {
            m_PostRainFireSuppressionTimer -= deltaTime;
        }
        if (!m_DustActive && m_TimeSinceLastRain >= 60.0f) {
            SpawnDustCloud();
        }
    }

    if (m_DustActive) {
        const float speed = 15.0f;
        m_DustPosition += m_DustDirection * speed * deltaTime;
        if (m_DustEmitterId != -1) {
            ParticleProps props = ParticleLibrary::GetDustStormProps();
            props.position = m_DustPosition;
            GetOrCreateSystem(props)->UpdateEmitter(m_DustEmitterId, props, 500.0f);
        }
        if (glm::length(m_DustPosition) > 150.0f) StopDust();
    }

    m_SeasonTimer += deltaTime;
    const float fullSeasonDuration = m_TimeConfig.dayLengthSeconds * static_cast<float>(m_TimeConfig.daysPerSeason);

    if (m_SeasonTimer >= fullSeasonDuration) {
        m_SeasonTimer = 0.0f;
        m_CurrentSeason = static_cast<Season>((static_cast<int>(m_CurrentSeason) + 1) % 4);

        if (m_IsPrecipitating) {
            StopPrecipitation();
            if (m_CurrentSeason == Season::WINTER) AddSnow();
            else AddRain();
        }
    }

    m_WeatherTimer += deltaTime;
    if (m_WeatherTimer >= m_CurrentWeatherDurationTarget) {
        m_WeatherTimer = 0.0f;
        m_IsPrecipitating = !m_IsPrecipitating;
        PickNextWeatherDuration();

        if (m_IsPrecipitating) {
            if (m_CurrentSeason == Season::WINTER) AddSnow();
            else AddRain();
        }
        else {
            StopPrecipitation();
        }
    }

    float sunHeight = 0.0f;
    Entity sunEntity = GetEntityByName("Sun");

    if (sunEntity != MAX_ENTITIES && m_Registry.HasComponent<TransformComponent>(sunEntity)) {
        const auto& sunTransform = m_Registry.GetComponent<TransformComponent>(sunEntity);
        const float rawHeight = sunTransform.matrix[3][1] / 275.0f;
        sunHeight = std::clamp(rawHeight, -1.0f, 1.0f);
    }

    float seasonBaseTemp = 0.0f;
    glm::vec3 targetSunColor = glm::vec3(1.0f);

    switch (m_CurrentSeason) {
    case Season::SUMMER:
        seasonBaseTemp = m_SeasonConfig.summerBaseTemp;
        targetSunColor = glm::vec3(1.0f, 0.95f, 0.8f);
        break;
    case Season::AUTUMN:
        seasonBaseTemp = (m_SeasonConfig.summerBaseTemp + m_SeasonConfig.winterBaseTemp) * 0.5f;
        targetSunColor = glm::vec3(1.0f, 0.85f, 0.7f);
        break;
    case Season::WINTER:
        seasonBaseTemp = m_SeasonConfig.winterBaseTemp;
        targetSunColor = glm::vec3(0.75f, 0.85f, 1.0f);
        break;
    case Season::SPRING:
        seasonBaseTemp = (m_SeasonConfig.summerBaseTemp + m_SeasonConfig.winterBaseTemp) * 0.5f;
        targetSunColor = glm::vec3(1.0f, 0.98f, 0.9f);
        break;
    }

    m_WeatherIntensity = seasonBaseTemp + (sunHeight * m_SeasonConfig.dayNightTempDiff);

    if (m_IsPrecipitating) {
        targetSunColor = glm::vec3(0.4f, 0.45f, 0.55f);
        m_WeatherIntensity -= 10.0f;
    }

    if (sunEntity != MAX_ENTITIES && m_Registry.HasComponent<LightComponent>(sunEntity)) {
        auto& sunLight = m_Registry.GetComponent<LightComponent>(sunEntity);
        sunLight.color = glm::mix(sunLight.color, targetSunColor, deltaTime * 0.8f);
    }

    for (auto& sys : m_Systems) {
        sys->Update(*this, deltaTime);
    }

    UpdateThermodynamics(deltaTime, sunHeight);
    UpdateSimpleShadows();

    for (const auto& sys : particleSystems) {
        sys->Update(deltaTime);
    }
}

std::vector<Light> Scene::GetLights() const {
    std::vector<Light> lights;
    lights.reserve(m_LightEntities.size());

    Registry& reg = GetMutRegistry(m_Registry);

    for (Entity e : m_LightEntities) {
        if (reg.HasComponent<LightComponent>(e) && reg.HasComponent<TransformComponent>(e)) {
            auto& lightComp = reg.GetComponent<LightComponent>(e);
            auto& transComp = reg.GetComponent<TransformComponent>(e);

            Light vLight{};
            vLight.position = glm::vec3(transComp.matrix[3]);
            vLight.color = lightComp.color;
            vLight.intensity = lightComp.intensity;
            vLight.type = lightComp.type;
            vLight.layerMask = SceneLayers::INSIDE;
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
    // You can implement light-specific logic here later.
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
        m_Registry.GetComponent<RenderComponent>(e).castsShadow = casts;
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

void Scene::UpdateThermodynamics(float deltaTime, float sunHeight) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    static float printTimer = 0.0f;
    printTimer += deltaTime;
    const bool shouldPrint = (printTimer > 1.0f);
    if (shouldPrint) printTimer = 0.0f;

    for (Entity e = 0; e < m_Registry.GetEntityCount(); ++e) {
        if (!m_Registry.HasComponent<ThermoComponent>(e) ||
            !m_Registry.HasComponent<TransformComponent>(e) ||
            !m_Registry.HasComponent<RenderComponent>(e)) {
            continue;
        }

        auto& thermo = m_Registry.GetComponent<ThermoComponent>(e);
        if (!thermo.isFlammable) continue;

        auto& transform = m_Registry.GetComponent<TransformComponent>(e);
        auto& render = m_Registry.GetComponent<RenderComponent>(e);

        const glm::vec3 basePos = glm::vec3(transform.matrix[3]);

        switch (thermo.state) {
        case ObjectState::NORMAL:
        case ObjectState::HEATING: {
            const float responseSpeed = thermo.thermalResponse;
            const float effectiveIgnitionThreshold = 100.0f;
            float targetTemp = m_WeatherIntensity;

            if (sunHeight > 0.1f) {
                targetTemp += m_SunHeatBonus * sunHeight;
            }

            if (m_IsPrecipitating) {
                targetTemp -= 40.0f;
            }

            const float changeRate = responseSpeed * deltaTime;
            const float lerpFactor = glm::clamp(changeRate, 0.0f, 1.0f);
            thermo.currentTemp = glm::mix(thermo.currentTemp, targetTemp, lerpFactor);

            if (thermo.currentTemp > 45.0f) {
                thermo.state = ObjectState::HEATING;
            }
            else {
                thermo.state = ObjectState::NORMAL;
            }

            if (!m_IsPrecipitating && m_PostRainFireSuppressionTimer <= 0.0f && thermo.currentTemp >= effectiveIgnitionThreshold) {
                const float excessHeat = thermo.currentTemp - effectiveIgnitionThreshold;
                const float ignitionChancePerSecond = 0.05f + (excessHeat * 0.005f);

                if (chance(gen) < (ignitionChancePerSecond * deltaTime)) {
                    thermo.state = ObjectState::BURNING;
                    thermo.burnTimer = 0.0f;
                    thermo.fireEmitterId = AddFire(basePos, 0.1f);
                    thermo.smokeEmitterId = AddSmoke(basePos, 0.1f);
                }
            }
            break;
        }

        case ObjectState::BURNING: {
            if (m_IsPrecipitating) {
                StopObjectFire(e);
                thermo.state = ObjectState::NORMAL;
                thermo.currentTemp = m_WeatherIntensity;
                thermo.burnTimer = 0.0f;
                break;
            }

            thermo.currentTemp += thermo.selfHeatingRate * deltaTime;
            thermo.burnTimer += deltaTime;

            const float growth = glm::clamp(thermo.burnTimer / (thermo.maxBurnDuration * 0.6f), 0.0f, 1.0f);
            thermo.burnFactor = glm::clamp(thermo.burnTimer / thermo.maxBurnDuration, 0.0f, 1.0f);

            const float maxFireHeight = 3.0f;
            const float currentFireHeight = 0.2f + (maxFireHeight - 0.2f) * growth;

            if (thermo.fireEmitterId != -1) {
                ParticleProps fireProps = ParticleLibrary::GetFireProps();
                fireProps.position = basePos;
                fireProps.position.y += currentFireHeight * 0.5f;
                fireProps.positionVariation = glm::vec3(0.3f, currentFireHeight * 0.4f, 0.3f);

                const float particleScale = 1.0f + growth * 0.5f;
                fireProps.sizeBegin *= particleScale;
                fireProps.sizeEnd *= particleScale;

                const float rate = 50.0f + (300.0f * growth);
                GetOrCreateSystem(fireProps)->UpdateEmitter(thermo.fireEmitterId, fireProps, rate);
            }

            if (thermo.smokeEmitterId != -1) {
                ParticleProps smokeProps = ParticleLibrary::GetSmokeProps();
                smokeProps.position = basePos;
                smokeProps.position.y += currentFireHeight;

                const float smokeScale = 1.0f + growth * 2.0f;
                smokeProps.sizeBegin *= smokeScale;
                smokeProps.sizeEnd *= smokeScale;
                smokeProps.lifeTime = 8.0f;
                smokeProps.velocity.y = 3.0f;

                const float rate = 20.0f + (80.0f * growth);
                GetOrCreateSystem(smokeProps)->UpdateEmitter(thermo.smokeEmitterId, smokeProps, rate);
            }

            glm::vec3 lightPos = basePos;
            lightPos.y += currentFireHeight * 0.5f;

            if (thermo.fireLightEntity == -1 || thermo.fireLightEntity == MAX_ENTITIES) {
                std::string lightName = "FireLight_" + std::to_string(e);
                thermo.fireLightEntity = AddLight(lightName, lightPos, glm::vec3(1.0f, 0.5f, 0.1f), 0.0f, 1);
            }

            if (thermo.fireLightEntity != MAX_ENTITIES && m_Registry.HasComponent<LightComponent>(thermo.fireLightEntity)) {
                auto& fireLightTransform = m_Registry.GetComponent<TransformComponent>(thermo.fireLightEntity);
                auto& fireLightComp = m_Registry.GetComponent<LightComponent>(thermo.fireLightEntity);

                const float t = thermo.burnTimer;
                const float flicker = 1.0f + 0.3f * std::sin(t * 15.0f) + 0.15f * std::sin(t * 37.0f);
                const float targetIntensity = 50.05f * growth;

                fireLightTransform.matrix[3] = glm::vec4(lightPos, 1.0f);
                fireLightComp.intensity = targetIntensity * flicker;
            }

            if (thermo.burnTimer >= thermo.maxBurnDuration) {
                thermo.state = ObjectState::BURNT;

                if (thermo.fireEmitterId != -1) {
                    GetOrCreateSystem(ParticleLibrary::GetFireProps())->StopEmitter(thermo.fireEmitterId);
                    thermo.fireEmitterId = -1;
                }

                if (thermo.fireLightEntity != MAX_ENTITIES && m_Registry.HasComponent<LightComponent>(thermo.fireLightEntity)) {
                    m_Registry.GetComponent<LightComponent>(thermo.fireLightEntity).intensity = 0.0f;
                }

                if (thermo.smokeEmitterId != -1) {
                    ParticleProps smolder = ParticleLibrary::GetSmokeProps();
                    smolder.position = basePos;
                    smolder.sizeBegin *= 0.1f;
                    smolder.sizeEnd *= 0.2f;
                    smolder.lifeTime = 1.5f;
                    smolder.velocity.y = 0.5f;
                    smolder.positionVariation = glm::vec3(0.1f);
                    GetOrCreateSystem(smolder)->UpdateEmitter(thermo.smokeEmitterId, smolder, 20.0f);
                }

                thermo.storedOriginalGeometry = render.geometry;
                thermo.storedOriginalTransform = transform.matrix;

                if (dustGeometryPrototype) {
                    render.geometry = dustGeometryPrototype;
                }
                render.texturePath = sootTexturePath;

                transform.matrix = glm::translate(glm::mat4(1.0f), basePos);
                transform.matrix = glm::scale(transform.matrix, glm::vec3(0.003f));

                thermo.regrowTimer = 0.0f;
                thermo.burnFactor = 0.0f;
            }
            break;
        }

        case ObjectState::BURNT:
        case ObjectState::REGROWING: {
            const float changeRate = 0.5f * deltaTime;
            const float lerpFactor = glm::clamp(changeRate, 0.0f, 1.0f);
            thermo.currentTemp = glm::mix(thermo.currentTemp, m_WeatherIntensity, lerpFactor);

            float growthMultiplier = 0.0f;
            if (m_WeatherIntensity > 10.0f) {
                growthMultiplier = (m_WeatherIntensity - 10.0f) / 15.0f;
            }
            thermo.regrowTimer += deltaTime * growthMultiplier;

            if (thermo.state == ObjectState::BURNT && thermo.regrowTimer > 5.0f && thermo.smokeEmitterId != -1) {
                GetOrCreateSystem(ParticleLibrary::GetSmokeProps())->StopEmitter(thermo.smokeEmitterId);
                thermo.smokeEmitterId = -1;
            }

            if (thermo.state == ObjectState::BURNT) {
                if (thermo.regrowTimer >= 10.0f) {
                    thermo.state = ObjectState::REGROWING;
                    thermo.regrowTimer = 0.0f;
                    thermo.currentTemp = m_WeatherIntensity;

                    if (thermo.storedOriginalGeometry) {
                        render.geometry = thermo.storedOriginalGeometry;
                        thermo.storedOriginalGeometry = nullptr;
                    }
                    render.texturePath = render.originalTexturePath;
                }
            }
            else if (thermo.state == ObjectState::REGROWING) {
                const float growthTime = m_TimeConfig.dayLengthSeconds * 0.75f;

                float t = glm::clamp(thermo.regrowTimer / growthTime, 0.0f, 1.0f);
                t = t * t * (3.0f - 2.0f * t);

                const float currentScale = glm::mix(0.003f, 1.0f, t);
                transform.matrix = glm::scale(thermo.storedOriginalTransform, glm::vec3(currentScale));

                if (t >= 1.0f) {
                    thermo.state = ObjectState::NORMAL;
                    thermo.currentTemp = m_WeatherIntensity;
                }
            }
            break;
        }
        }
    }
}

std::string Scene::GetSeasonName() const {
    switch (m_CurrentSeason) {
    case Season::SUMMER: return "Summer";
    case Season::AUTUMN: return "Autumn";
    case Season::WINTER: return "Winter";
    case Season::SPRING: return "Spring";
    }
    return "Unknown";
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
    m_IsPrecipitating = false;
    m_WeatherTimer = 0.0f;
    PickNextWeatherDuration();
    StopDust();
    m_TimeSinceLastRain = 0.0f;
}