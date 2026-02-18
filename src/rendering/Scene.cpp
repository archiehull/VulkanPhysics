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

void Scene::PrintDebugInfo() const {
    std::cout << "\n================ SCENE DEBUG INFO ================\n";

    std::cout << "--- OBJECTS (" << objects.size() << ") ---\n";
    for (const auto& obj : objects) {
        const glm::vec3 pos = glm::vec3(obj->transform[3]);
        const glm::vec3 scale = glm::vec3(
            glm::length(glm::vec3(obj->transform[0])),
            glm::length(glm::vec3(obj->transform[1])),
            glm::length(glm::vec3(obj->transform[2]))
        );

        std::cout << " [OBJ] Name: " << obj->name
            << " | Vis: " << (obj->visible ? "TRUE" : "FALSE")
            << " | Pos: (" << pos.x << ", " << pos.y << ", " << pos.z << ")"
            << " | Scale: (" << scale.x << ", " << scale.y << ", " << scale.z << ")"
            << " | CastShadow: " << obj->castsShadow
            << "\n";
    }

    std::cout << "\n--- LIGHTS (" << m_SceneLights.size() << ") ---\n";
    for (const auto& light : m_SceneLights) {
        std::cout << " [LGT] Name: " << light.name
            << " | Pos: (" << light.vulkanLight.position.x << ", "
            << light.vulkanLight.position.y << ", "
            << light.vulkanLight.position.z << ")"
            << " | Intensity: " << light.vulkanLight.intensity
            << " | Color: (" << light.vulkanLight.color.x << ", "
            << light.vulkanLight.color.y << ", "
            << light.vulkanLight.color.z << ")"
            << "\n";
    }
    std::cout << "==================================================\n\n";
}

void Scene::ToggleGlobalShadingMode() {
    globalShadingMode = (globalShadingMode == 1) ? 0 : 1;

    // Update all existing objects, preserving special modes (>=2)
    // 0 = Gouraud, 1 = Phong
    for (const auto& obj : objects) {
        if (obj->shadingMode == 0 || obj->shadingMode == 1) {
            obj->shadingMode = globalShadingMode;
        }
    }
    std::cout << "Shading Mode Toggled: " << (globalShadingMode == 1 ? "Phong" : "Gouraud") << std::endl;
}

void Scene::AddObjectInternal(const std::string& name, std::unique_ptr<Geometry> geometry, const glm::vec3& position, const std::string& texturePath, bool isFlammable) {
    std::shared_ptr<Geometry> sharedGeo = std::move(geometry);
    auto obj = std::make_unique<SceneObject>(sharedGeo, texturePath, name);
    obj->transform = glm::translate(glm::mat4(1.0f), position);
    obj->isFlammable = isFlammable;

    // Use the global default instead of poly count check
    obj->shadingMode = globalShadingMode;

    objects.push_back(std::move(obj));
}

float Scene::RadiusAdjustment(const float radius, const float deltaY) const {
    const float planeY = deltaY;
    float terrainRadius = 0.0f;
    const float absDist = std::fabs(planeY);
    if (absDist < radius) {
        terrainRadius = std::sqrt(radius * radius - absDist * absDist);
    }
    else {
        terrainRadius = 0.0f; // plane is outside sphere — no intersection
    }
    return terrainRadius;
}

Scene::Scene(VkDevice vkDevice, VkPhysicalDevice physDevice)
    : device(vkDevice), physicalDevice(physDevice) {
}

void Scene::Initialize() {
    // Load Ash Pile for shared use in burning objects
    try {
        auto dustGeo = OBJLoader::Load(device, physicalDevice, "models/dust.obj");
        dustGeometryPrototype = std::shared_ptr<Geometry>(std::move(dustGeo));
    }
    catch (const std::exception& e) {
        // Wrap the error and pass it up to the Application.
        // We do NOT use std::cerr here.
        throw std::runtime_error(std::string("Warning: Failed to load dust prototype: ") + e.what());
    }
}

// Destructor implementation removed (now = default in header)

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
        // 1. Pick Position
        const float r = std::sqrt(distScale(gen)) * (terrainRadius * 0.9f);
        const float theta = distAngle(gen);
        const float x = r * cos(theta);
        const float z = r * sin(theta);

        // 2. Calculate Height
        const float yOffset = GeometryGenerator::GetTerrainHeight(x, z, terrainRadius, heightScale, noiseFreq);
        const float y = deltaY + yOffset;

        // 3. Select Object
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

        // 4. Randomize Scale
        glm::vec3 scale;
        scale.x = glm::mix(config.minScale.x, config.maxScale.x, distScale(gen));
        scale.y = glm::mix(config.minScale.y, config.maxScale.y, distScale(gen));
        scale.z = glm::mix(config.minScale.z, config.maxScale.z, distScale(gen));

        // 5. Spawn Object
        const std::string name = "ProcObj_" + std::to_string(i);
        AddModel(name, glm::vec3(x, y, z), glm::vec3(0.0f), scale, config.modelPath, config.texturePath, config.isFlammable);

        // Capture the main object pointer NOW, before adding the shadow
        SceneObject* mainObj = nullptr;
        if (!objects.empty()) {
            mainObj = objects.back().get();
        }

        // Add Simple Shadow
        const float shadowRadius = std::max(std::max(scale.x, scale.z) * 1.5f, 0.5f);
        AddSimpleShadow(name, shadowRadius);

        // 6. Overwrite Transform on the MAIN OBJECT (not objects.back(), which is the shadow)
        if (mainObj) {
            // Assign unique thermal response if the object is flammable
            if (config.isFlammable) {
                mainObj->thermalResponse = distThermal(gen);
            }

            glm::mat4 m = glm::mat4(1.0f);

            // A. Translate to position
            m = glm::translate(m, glm::vec3(x, y, z));

            // B. Apply World Yaw (Random Rotation around Y)
            const float randomYaw = distRot(gen);
            m = glm::rotate(m, glm::radians(randomYaw), glm::vec3(0.0f, 1.0f, 0.0f));

            // C. Apply Base Rotation Correction (e.g. Stand up the cactus)
            if (glm::length(config.baseRotation) > 0.001f) {
                m = glm::rotate(m, glm::radians(config.baseRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                m = glm::rotate(m, glm::radians(config.baseRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                m = glm::rotate(m, glm::radians(config.baseRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            }

            // D. Scale
            m = glm::scale(m, scale);

            mainObj->transform = m;
        }
    }
}



void Scene::AddTerrain(const std::string& name, float radius, int rings, int segments, float heightScale, float noiseFreq, const glm::vec3& position, const std::string& texturePath) {
    AddObjectInternal(name, GeometryGenerator::CreateTerrain(device, physicalDevice, radius - 1, rings, segments, heightScale, noiseFreq), position, texturePath, false);

    // DISABLE generic cylinder collision for the Terrain object itself 
    // (We will use the Math-based height check instead)
    if (!objects.empty()) {
        const auto& obj = objects.back();
        obj->hasCollision = false;
    }

    // STORE params for the Camera Controller
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
    AddObjectInternal(name, GeometryGenerator::CreatePedestal(device, physicalDevice, topRadius, baseWidth, height, 512, 512), position, texturePath, false);

    if (!objects.empty()) {
        const auto& obj = objects.back();
        // Use the wider base for collision to prevent clipping
        obj->collisionRadius = std::max(topRadius, baseWidth);
        obj->collisionHeight = height;
        obj->hasCollision = true;
    }
}

void Scene::AddCube(const std::string& name, const glm::vec3& position, const glm::vec3& scale, const std::string& texturePath) {
    AddObjectInternal(name, GeometryGenerator::CreateCube(device, physicalDevice), position, texturePath, false);

    if (!objects.empty()) {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
        t = glm::scale(t, scale);
        objects.back()->transform = t;
        // Logic removed: UpdateShadingMode(objects.back().get());
        // Since AddObjectInternal already sets the global default, we are good.
    }
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
        std::unique_ptr<Geometry> geometry;

        // Check file extension
        std::string ext = modelPath.substr(modelPath.find_last_of(".") + 1);
        if (ext == "sjg") {
            geometry = SJGLoader::Load(device, physicalDevice, modelPath);
        }
        else {
            // Default to OBJ
            geometry = OBJLoader::Load(device, physicalDevice, modelPath);
        }

        // Explicit shared_ptr conversion
        std::shared_ptr<Geometry> sharedGeo = std::move(geometry);
        auto obj = std::make_unique<SceneObject>(sharedGeo, texturePath, name);

        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, position);
        transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        transform = glm::scale(transform, scale);

        obj->transform = transform;
        obj->isFlammable = isFlammable;

        obj->shadingMode = globalShadingMode; // Use global setting

        objects.push_back(std::move(obj));
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to add model '" << modelPath << "': " << e.what() << std::endl;
    }
}

void Scene::AddSimpleShadow(const std::string& objectName, float radius) {
    const auto it = std::find_if(objects.begin(), objects.end(),
        [&](const std::unique_ptr<SceneObject>& obj) { return obj->name == objectName; });

    if (it == objects.end()) return;
    SceneObject* const targetObj = it->get();

    // Create Disk Geometry
    auto diskGeo = GeometryGenerator::CreateDisk(device, physicalDevice, radius, 16);

    // Create Shadow Object
    std::string shadowName = objectName + "_Shadow";
    auto shadowObj = std::make_unique<SceneObject>(std::move(diskGeo), "textures/shadow.jpg", shadowName);

    // Configure Properties
    shadowObj->castsShadow = false;
    shadowObj->receiveShadows = false;
    shadowObj->shadingMode = 0;
    shadowObj->isFlammable = false;
    shadowObj->hasCollision = false;

    // Force it to be black using the burn factor
    shadowObj->burnFactor = 1.0f;

    // Initially hidden
    shadowObj->visible = false;

    objects.push_back(std::move(shadowObj));
    targetObj->simpleShadowId = static_cast<int>(objects.size() - 1);
}

void Scene::ToggleSimpleShadows() {
    m_UseSimpleShadows = !m_UseSimpleShadows;

    for (const auto& obj : objects) {
        // Find objects that HAVE a simple shadow
        if (obj->simpleShadowId != -1 && obj->simpleShadowId < objects.size()) {
            SceneObject* const shadow = objects[obj->simpleShadowId].get();

            if (m_UseSimpleShadows) {
                // SIMPLE MODE: Hide normal shadow, Show simple shadow
                obj->castsShadow = false;
                shadow->visible = obj->visible;
            }
            else {
                // NORMAL MODE: Show normal shadow, Hide simple shadow
                obj->castsShadow = true;
                shadow->visible = false;
            }
        }
    }
    std::cout << "Shadow Mode: " << (m_UseSimpleShadows ? "Simple" : "Normal") << std::endl;
}

void Scene::UpdateSimpleShadows() {
    if (!m_UseSimpleShadows) return;

    // 1. Find Dominant Light (Sun)
    glm::vec3 lightPos = glm::vec3(0.0f, 100.0f, 0.0f);
    bool sunIsUp = false;

    for (const auto& light : m_SceneLights) {
        // We consider the sun "Up" if it's slightly above the horizon (-20.0f)
        if (light.name == "Sun" && light.vulkanLight.position.y > -20.0f) {
            lightPos = light.vulkanLight.position;
            sunIsUp = true;
            break;
        }
    }

    // 2. Iterate ALL objects and enforce state
    for (const auto& obj : objects) {
        // Skip if this object doesn't have a shadow
        if (obj->simpleShadowId == -1 || obj->simpleShadowId >= objects.size()) {
            continue;
        }

        SceneObject* const shadow = objects[obj->simpleShadowId].get();

        if (sunIsUp && obj->visible) {

            // --- Calculate Shadow Transform ---
            // Bias to sit above terrain
            // Direction from Object to Light (pointing UP towards light)
            const glm::vec3 rawLightDir = glm::vec3(obj->transform[3]) + glm::vec3(0.0f, 0.15f, 0.0f) - lightPos;
            const glm::vec3 lightDir3D = glm::normalize(rawLightDir);

            // Flatten direction onto the XZ plane
            glm::vec3 flatDir = glm::vec3(lightDir3D.x, 0.0f, lightDir3D.z);
            if (glm::length(flatDir) > 0.001f) {
                flatDir = glm::normalize(flatDir);
            }
            else {
                flatDir = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            // Rotation (Yaw)
            const float angle = std::atan2(flatDir.x, flatDir.z);

            // Stretch (based on how low the sun is)
            // lightDir3D.y is negative if light is above. 
            // We use abs() to get the vertical component magnitude.
            const float dotY = std::abs(lightDir3D.y);
            float stretch = 1.0f + (1.0f - dotY) * 8.0f;
            stretch = std::clamp(stretch, 1.0f, 12.0f);

            // --- Radius Fix (Applied Here) ---
            const float parentScale = glm::length(glm::vec3(obj->transform[0]));
            const float shadowRadius = std::max(parentScale * 1.5f, 0.5f);

            // Shift Center to anchor the back edge
            const float shiftAmount = shadowRadius * (stretch - 1.0f);
            const glm::vec3 finalPos = glm::vec3(obj->transform[3]) + glm::vec3(0.0f, 0.15f, 0.0f) + (flatDir * shiftAmount);

            // Update Transform
            glm::mat4 m = glm::mat4(1.0f);
            m = glm::translate(m, finalPos);
            m = glm::rotate(m, angle, glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::scale(m, glm::vec3(1.0f, 1.0f, stretch));

            shadow->transform = m;
            shadow->visible = true; // FORCE VISIBLE
        }
        else {
            // If Sun is down OR Parent is hidden, hide shadow
            shadow->visible = false;
        }
    }
}

int Scene::AddLight(const std::string& name, const glm::vec3& position, const glm::vec3& color, float intensity, int type) {
    if (m_SceneLights.size() >= MAX_LIGHTS) {
        std::cerr << "Warning: Maximum number of lights (" << MAX_LIGHTS << ") reached. Light not added." << std::endl;
        return -1;
    }

    SceneLight newSceneLight{};
    newSceneLight.name = name;
    newSceneLight.vulkanLight.position = position;
    newSceneLight.vulkanLight.color = color;
    newSceneLight.vulkanLight.intensity = intensity;
    newSceneLight.vulkanLight.type = type; // We will use Type 1 for Point Lights

    newSceneLight.vulkanLight.layerMask = SceneLayers::INSIDE;
    newSceneLight.layerMask = SceneLayers::INSIDE;

    m_SceneLights.push_back(newSceneLight);

    // Return the index of the newly added light
    return static_cast<int>(m_SceneLights.size() - 1);
}

void Scene::SetObjectCollision(const std::string& name, bool enabled) {
    const auto it = std::find_if(objects.begin(), objects.end(), [&](const auto& obj) { return obj->name == name; });
    if (it != objects.end()) {
        (*it)->hasCollision = enabled;
    }
}

void Scene::SetObjectCollisionSize(const std::string& name, float radius, float height) {
    const auto it = std::find_if(objects.begin(), objects.end(), [&](const auto& obj) { return obj->name == name; });
    if (it != objects.end()) {
        (*it)->collisionRadius = radius;
        (*it)->collisionHeight = height;
    }
}

void Scene::SetObjectTexture(const std::string& objectName, const std::string& texturePath) {
    const auto it = std::find_if(objects.begin(), objects.end(),
        [&](const std::unique_ptr<SceneObject>& obj) { return obj->name == objectName; });

    if (it != objects.end()) {
        (*it)->texturePath = texturePath;
        // Optionally update originalTexturePath if you want it to persist through burning
        (*it)->originalTexturePath = texturePath;
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
    // Check if a system with this texture already exists
    for (const auto& sys : particleSystems) {
        if (sys->GetTexturePath() == props.texturePath) {
            return sys.get();
        }
    }

    // Create new system
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

void Scene::Ignite(SceneObject* obj) {
    if (!obj || !obj->isFlammable) return;
    if (obj->state == ObjectState::BURNING || obj->state == ObjectState::BURNT || obj->state == ObjectState::REGROWING) return;

    //std::cout << "Igniting object: " << obj->name << std::endl;

    obj->state = ObjectState::BURNING;
    obj->burnTimer = 0.0f;
    obj->currentTemp = obj->ignitionThreshold + 50.0f; // Jumpstart temp to keep it burning

    // Spawn initial particles immediately so we don't wait for the next frame
    const glm::vec3 pos = glm::vec3(obj->transform[3]);
    if (obj->fireEmitterId == -1) {
        obj->fireEmitterId = AddFire(pos, 0.1f);
    }
    if (obj->smokeEmitterId == -1) {
        obj->smokeEmitterId = AddSmoke(pos, 0.1f);
    }
}

void Scene::ToggleWeather() {
    m_IsPrecipitating = !m_IsPrecipitating;

    // Reset the timer and pick a new target for this forced state
    m_WeatherTimer = 0.0f;
    PickNextWeatherDuration();

    if (m_IsPrecipitating) {
        if (m_CurrentSeason == Season::WINTER) {
            AddSnow();
        }
        else {
            AddRain();
        }
        std::cout << "Weather Toggled: Precipitation ON (" << m_CurrentWeatherDurationTarget << "s)" << std::endl;
    }
    else {
        StopPrecipitation();
        std::cout << "Weather Toggled: Clear Skies (" << m_CurrentWeatherDurationTarget << "s)" << std::endl;
    }
}

void Scene::AddRain() {
    if (m_RainEmitterId != -1) return; // Already raining

    ParticleProps rain = ParticleLibrary::GetRainProps();
    rain.position = glm::vec3(0.0f, -50.0f, 0.0f);
    rain.positionVariation = glm::vec3(60.0f, 0.0f, 60.0f);
    rain.velocityVariation = glm::vec3(1.0f, 2.0f, 1.0f);

    auto* const sys = GetOrCreateSystem(rain);
    sys->SetSimulationBounds(glm::vec3(0.0f), 150.0f);

    // Store the ID
    m_RainEmitterId = sys->AddEmitter(rain, 4000.0f);
}

void Scene::AddSnow() {
    if (m_SnowEmitterId != -1) return; // Already snowing

    ParticleProps snow = ParticleLibrary::GetSnowProps();
    snow.position = glm::vec3(0.0f, -50.0f, 0.0f);
    snow.positionVariation = glm::vec3(100.0f, 0.0f, 100.0f);
    snow.velocityVariation = glm::vec3(1.0f, 0.2f, 1.0f);

    auto* const sys = GetOrCreateSystem(snow);
    sys->SetSimulationBounds(glm::vec3(0.0f), 150.0f);

    // Store the ID
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
    dust.position = glm::vec3(0.0f, 5.0f, 0.0f); // Near ground
    dust.velocityVariation.x = 80.0f;
    dust.velocityVariation.z = 80.0f;
    dust.velocityVariation.y = 10.0f; // Height of dust cloud

    // Set bounds for dust (matches CrystalBall radius)
    auto* const sys = GetOrCreateSystem(dust);
    sys->SetSimulationBounds(glm::vec3(0.0f), 150.0f);
    sys->AddEmitter(dust, 200.0f);
}

void Scene::SpawnDustCloud() {
    if (m_DustActive) return;

    std::cout << "Spawning Dust Cloud!" << std::endl;

    m_DustActive = true;
    m_DustPosition = glm::vec3(0.0f, -70.0f, 0.0f);

    // Pick Random Direction
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());

    const float angle = distAngle(gen);
    m_DustDirection = glm::vec3(cos(angle), 0.0f, sin(angle));

    ParticleProps dust = ParticleLibrary::GetDustStormProps();

    // We only need to override position/movement, not visuals!
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

        // Reset rain timer so it doesn't immediately respawn
        m_TimeSinceLastRain = 0.0f;
    }
}

glm::vec3 Scene::InitializeOrbit(OrbitData& data, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad) const {
    data.isOrbiting = true;
    data.center = center;
    data.radius = radius;
    data.speed = speedRadPerSec;
    const float axisLen = glm::length(axis);
    data.axis = (axisLen > 1e-6f) ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);

    // Normalize and scale start vector to match radius
    if (glm::length(startVector) > 1e-6f) {
        data.startVector = glm::normalize(startVector) * radius;
    }
    else {
        data.startVector = glm::vec3(radius, 0.0f, 0.0f);
    }

    data.initialAngle = initialAngleRad;
    data.currentAngle = initialAngleRad;

    const glm::quat rotation = glm::angleAxis(data.initialAngle, data.axis);

    const glm::vec3 offset = rotation * data.startVector;
    return data.center + offset;
}

void Scene::SetObjectOrbit(const std::string& name, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad) {
    const auto it = std::find_if(objects.begin(), objects.end(),
        [&name](const std::unique_ptr<SceneObject>& obj) {
            return obj->name == name;
        });

    if (it != objects.end()) {
        SceneObject* const objectPtr = it->get();
        const glm::vec3 initialPosition = InitializeOrbit(objectPtr->OrbitData, center, radius, speedRadPerSec, axis, startVector, initialAngleRad);
        objectPtr->transform[3] = glm::vec4(initialPosition, 1.0f);
    }
    else {
        std::cerr << "Error: Scene object with name '" << name << "' not found for Orbit assignment." << std::endl;
    }
}

void Scene::SetLightOrbit(const std::string& name, const glm::vec3& center, float radius, float speedRadPerSec, const glm::vec3& axis, const glm::vec3& startVector, float initialAngleRad) {
    const auto it = std::find_if(m_SceneLights.begin(), m_SceneLights.end(),
        [&name](const SceneLight& light) {
            return light.name == name;
        });

    if (it != m_SceneLights.end()) {
        SceneLight& sceneLight = const_cast<SceneLight&>(*it);
        const glm::vec3 initialPosition = InitializeOrbit(sceneLight.OrbitData, center, radius, speedRadPerSec, axis, startVector, initialAngleRad);
        sceneLight.vulkanLight.position = initialPosition;
    }
    else {
        std::cerr << "Error: Scene light with name '" << name << "' not found for Orbit assignment." << std::endl;
    }
}

void Scene::SetOrbitSpeed(const std::string& name, float speedRadPerSec) {
    const auto itObj = std::find_if(objects.begin(), objects.end(),
        [&](const std::unique_ptr<SceneObject>& obj) { return obj->name == name; });

    if (itObj != objects.end()) {
        (*itObj)->OrbitData.speed = speedRadPerSec;
    }

    const auto itLight = std::find_if(m_SceneLights.begin(), m_SceneLights.end(),
        [&](const SceneLight& light) { return light.name == name; });

    if (itLight != m_SceneLights.end()) {
        const_cast<SceneLight&>(*itLight).OrbitData.speed = speedRadPerSec;
    }
}

void Scene::SetTimeConfig(const TimeConfig& config) {
    m_TimeConfig = config;
}

void Scene::SetWeatherConfig(const WeatherConfig& config) {
    m_WeatherConfig = config;
    PickNextWeatherDuration(); // Initialize with a valid duration
}

void Scene::SetSeasonConfig(const SeasonConfig& config) {
    m_SeasonConfig = config;
}

void Scene::PickNextWeatherDuration() {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    if (m_IsPrecipitating) {
        // We are currently raining/snowing, decide how long it lasts
        std::uniform_real_distribution<float> dist(m_WeatherConfig.minPrecipitationDuration, m_WeatherConfig.maxPrecipitationDuration);
        m_CurrentWeatherDurationTarget = dist(gen);
    }
    else {
        // We are currently clear, decide how long until next storm
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
    // --- Dust & Fire Suppression Logic (Existing) ---
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


    // --- 1. Season Cycle Update ---
    // A season lasts for (DaysPerSeason * DayLength)
    m_SeasonTimer += deltaTime;

    const float fullSeasonDuration = m_TimeConfig.dayLengthSeconds * static_cast<float>(m_TimeConfig.daysPerSeason);

    if (m_SeasonTimer >= fullSeasonDuration) {
        m_SeasonTimer = 0.0f;
        m_CurrentSeason = static_cast<Season>((static_cast<int>(m_CurrentSeason) + 1) % 4);

        // Refresh precipitation type if active
        if (m_IsPrecipitating) {
            StopPrecipitation();
            if (m_CurrentSeason == Season::WINTER) AddSnow();
            else AddRain();
        }
        // std::cout << "Season Changed to: " << GetSeasonName() << std::endl;
    }

    // --- 2. Weather Cycle Update ---
    m_WeatherTimer += deltaTime;

    if (m_WeatherTimer >= m_CurrentWeatherDurationTarget) {
        m_WeatherTimer = 0.0f;
        const bool allowToggle = true;

        // Constraint: If it's clear, don't rain if the Sun is just starting a new day (optional polish)
        if (!m_IsPrecipitating) {
            // (Logic simplified: just allow rain based on timer)
        }

        if (allowToggle) {
            m_IsPrecipitating = !m_IsPrecipitating;
            PickNextWeatherDuration(); // Choose random duration for this new state

            if (m_IsPrecipitating) {
                if (m_CurrentSeason == Season::WINTER) AddSnow();
                else AddRain();
            }
            else {
                StopPrecipitation();
            }
        }
    }

    // --- 3. Calculate Weather Intensity (Temp) ---
    float sunHeight = 0.0f;
    if (!m_SceneLights.empty()) {
        const float rawHeight = m_SceneLights[0].vulkanLight.position.y / 275.0f;
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
        targetSunColor = glm::vec3(0.4f, 0.45f, 0.55f); // Stormy
        m_WeatherIntensity -= 10.0f; // Rain cooling
    }

    // --- 4. Sun Tint & Orbit Updates ---
    const auto sunIt = std::find_if(m_SceneLights.begin(), m_SceneLights.end(),
        [](const SceneLight& l) { return l.name == "Sun"; });

    if (sunIt != m_SceneLights.end()) {
        sunIt->vulkanLight.color = glm::mix(sunIt->vulkanLight.color, targetSunColor, deltaTime * 0.8f);
    }

    auto CalculateNewPos = [&](OrbitData& data) -> glm::vec3 {
        data.currentAngle += data.speed * deltaTime;
        const glm::quat rotation = glm::angleAxis(data.currentAngle, data.axis);
        const glm::vec3 offset = rotation * data.startVector;
        return data.center + offset;
        };

    for (auto& sceneLight : m_SceneLights) {
        if (sceneLight.OrbitData.isOrbiting) {
            sceneLight.vulkanLight.position = CalculateNewPos(sceneLight.OrbitData);
        }
    }

    for (const auto& obj : objects) {
        if (obj->OrbitData.isOrbiting) {
            const glm::vec3 newPos = CalculateNewPos(obj->OrbitData);
            obj->transform[3] = glm::vec4(newPos, 1.0f);
        }
    }

    // 5. Update Thermodynamics / Shadows / Particles
    UpdateThermodynamics(deltaTime, sunHeight);
    UpdateSimpleShadows();
    for (const auto& sys : particleSystems) {
        sys->Update(deltaTime);
    }
}

std::vector<Light> Scene::GetLights() const {
    std::vector<Light> lights;
    lights.reserve(m_SceneLights.size());
    for (const auto& sceneLight : m_SceneLights) {
        lights.push_back(sceneLight.vulkanLight);
    }
    return lights;
}

void Scene::Clear() {
    for (const auto& obj : objects) {
        if (obj && obj->geometry) {
            obj->geometry->Cleanup();
        }
    }
    objects.clear();
    particleSystems.clear();
}

void Scene::SetObjectTransform(size_t index, const glm::mat4& transform) {
    if (index < objects.size()) {
        objects[index]->transform = transform;
    }
}

void Scene::SetObjectLayerMask(const std::string& name, int mask) {
    const auto it = std::find_if(objects.begin(), objects.end(),
        [&name](const std::unique_ptr<SceneObject>& obj) {
            return obj->name == name;
        });
    if (it != objects.end()) {
        (*it)->layerMask = mask;
    }
}

void Scene::SetLightLayerMask(const std::string& name, int mask) {
    const auto it = std::find_if(m_SceneLights.begin(), m_SceneLights.end(),
        [&name](const SceneLight& light) {
            return light.name == name;
        });

    if (it != m_SceneLights.end()) {
        // Update both the SceneLight wrapper and the internal Vulkan struct
        const_cast<SceneLight&>(*it).layerMask = mask;
        const_cast<SceneLight&>(*it).vulkanLight.layerMask = mask;
    }
}

void Scene::SetObjectVisible(size_t index, bool visible) {
    if (index < objects.size()) {
        objects[index]->visible = visible;
    }
}

void Scene::SetObjectCastsShadow(const std::string& name, bool casts) {
    const auto it = std::find_if(objects.begin(), objects.end(),
        [&name](const std::unique_ptr<SceneObject>& obj) {
            return obj->name == name;
        });

    if (it != objects.end()) {
        (*it)->castsShadow = casts;
    }
    else {
        std::cerr << "Warning: Scene object with name '" << name << "' not found to set castsShadow=" << casts << std::endl;
    }
}

void Scene::SetObjectReceivesShadows(const std::string& name, bool receives) {
    const auto it = std::find_if(objects.begin(), objects.end(),
        [&name](const std::unique_ptr<SceneObject>& obj) {
            return obj->name == name;
        });

    if (it != objects.end()) {
        (*it)->receiveShadows = receives;
    }
}

void Scene::SetObjectShadingMode(const std::string& name, int mode) {
    const auto it = std::find_if(objects.begin(), objects.end(),
        [&name](const std::unique_ptr<SceneObject>& obj) {
            return obj->name == name;
        });

    if (it != objects.end()) {
        (*it)->shadingMode = mode;
    }
}

void Scene::StopObjectFire(SceneObject* obj) {
    if (!obj) return;

    if (obj->fireEmitterId != -1) {
        GetOrCreateSystem(ParticleLibrary::GetFireProps())->StopEmitter(obj->fireEmitterId);
        obj->fireEmitterId = -1;
    }
    if (obj->smokeEmitterId != -1) {
        GetOrCreateSystem(ParticleLibrary::GetSmokeProps())->StopEmitter(obj->smokeEmitterId);
        obj->smokeEmitterId = -1;
    }
    if (obj->fireLightIndex != -1 && obj->fireLightIndex < m_SceneLights.size()) {
        m_SceneLights[obj->fireLightIndex].vulkanLight.intensity = 0.0f;
    }
}

void Scene::UpdateThermodynamics(float deltaTime, float sunHeight) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> chance(0.0f, 1.0f);

    // Debug Timer: Prints temp of the first procedural object once per second
    static float printTimer = 0.0f;
    printTimer += deltaTime;
    const bool shouldPrint = (printTimer > 1.0f);
    if (shouldPrint) printTimer = 0.0f;

    for (auto& obj : objects) {
        if (!obj->isFlammable) continue;

        switch (obj->state) {
        case ObjectState::NORMAL:
        case ObjectState::HEATING: {
            // Use the object's unique thermal response
            const float responseSpeed = obj->thermalResponse;
            // Lower threshold to guarantee ignition during "hot" moments
            const float effectiveIgnitionThreshold = 100.0f;

            float targetTemp = m_WeatherIntensity;

            // Massive Sun Bonus (+60C) if sun is high
            if (sunHeight > 0.1f) {
                targetTemp += m_SunHeatBonus * sunHeight;
            }

            // --- Cooling Effect ---
            // If raining/snowing, apply a strong cooling modifier
            if (m_IsPrecipitating) {
                targetTemp -= 40.0f;
            }

            // STABLE MATH: Interpolate towards target
            const float changeRate = responseSpeed * deltaTime;
            const float lerpFactor = glm::clamp(changeRate, 0.0f, 1.0f);
            obj->currentTemp = glm::mix(obj->currentTemp, targetTemp, lerpFactor);

            // Visual State Update
            if (obj->currentTemp > 45.0f) {
                obj->state = ObjectState::HEATING;
            }
            else {
                obj->state = ObjectState::NORMAL;
            }

            // IGNITION CHECK
            // -- Block Ignition ---
            // Only allow ignition if it is NOT precipitating
            if (!m_IsPrecipitating && m_PostRainFireSuppressionTimer <= 0.0f && obj->currentTemp >= effectiveIgnitionThreshold) {
                const float excessHeat = obj->currentTemp - effectiveIgnitionThreshold;

                // High Chance: % base + 5% per degree of excess heat
                const float ignitionChancePerSecond = 0.05f + (excessHeat * 0.005f);

                if (chance(gen) < (ignitionChancePerSecond * deltaTime)) {
                    obj->state = ObjectState::BURNING;
                    obj->burnTimer = 0.0f;

                    // Spawn Initial Particles
                    const glm::vec3 pos = glm::vec3(obj->transform[3]);
                    obj->fireEmitterId = AddFire(pos, 0.1f);
                    obj->smokeEmitterId = AddSmoke(pos, 0.1f);
                }
            }
            break;
        }

        case ObjectState::BURNING: {
            // --- Extinguish Fire ---
            if (m_IsPrecipitating) {
                StopObjectFire(obj.get());

                // 3. Reset State (Saved!)
                obj->state = ObjectState::NORMAL;
                obj->currentTemp = m_WeatherIntensity; // Rapid cooling to ambient
                obj->burnTimer = 0.0f;

                // Break out of the switch so we don't process the burning logic below
                break;
            }

            // Self-Heating: Fire generates its own heat
            obj->currentTemp += obj->selfHeatingRate * deltaTime;
            obj->burnTimer += deltaTime;

            // Calculate Growth Factors
            const float growth = glm::clamp(obj->burnTimer / (obj->maxBurnDuration * 0.6f), 0.0f, 1.0f);
            obj->burnFactor = glm::clamp(obj->burnTimer / obj->maxBurnDuration, 0.0f, 1.0f);

            // Fire Height Calculation
            const float maxFireHeight = 3.0f;
            const float currentFireHeight = 0.2f + (maxFireHeight - 0.2f) * growth;
            const glm::vec3 basePos = glm::vec3(obj->transform[3]);

            // Update Fire Particles
            if (obj->fireEmitterId != -1) {
                ParticleProps fireProps = ParticleLibrary::GetFireProps();
                fireProps.position = basePos;
                fireProps.position.y += currentFireHeight * 0.5f;
                fireProps.positionVariation = glm::vec3(0.3f, currentFireHeight * 0.4f, 0.3f);

                const float particleScale = 1.0f + growth * 0.5f;
                fireProps.sizeBegin *= particleScale;
                fireProps.sizeEnd *= particleScale;

                const float rate = 50.0f + (300.0f * growth);
                GetOrCreateSystem(fireProps)->UpdateEmitter(obj->fireEmitterId, fireProps, rate);
            }

            // Update Smoke Particles
            if (obj->smokeEmitterId != -1) {
                ParticleProps smokeProps = ParticleLibrary::GetSmokeProps();
                smokeProps.position = basePos;
                smokeProps.position.y += currentFireHeight;

                const float smokeScale = 1.0f + growth * 2.0f;
                smokeProps.sizeBegin *= smokeScale;
                smokeProps.sizeEnd *= smokeScale;
                smokeProps.lifeTime = 8.0f;
                smokeProps.velocity.y = 3.0f;

                const float rate = 20.0f + (80.0f * growth);
                GetOrCreateSystem(smokeProps)->UpdateEmitter(obj->smokeEmitterId, smokeProps, rate);
            }

            // Update Fire Light
            glm::vec3 lightPos = basePos;
            lightPos.y += currentFireHeight * 0.5f;

            if (obj->fireLightIndex == -1) {
                obj->fireLightIndex = AddLight(obj->name + "_Fire", lightPos, glm::vec3(1.0f, 0.5f, 0.1f), 0.0f, 1);
            }

            if (obj->fireLightIndex != -1 && obj->fireLightIndex < m_SceneLights.size()) {
                const float t = obj->burnTimer;
                const float flicker = 1.0f + 0.3f * std::sin(t * 15.0f) + 0.15f * std::sin(t * 37.0f);
                const float targetIntensity = 50.05f * growth;
                m_SceneLights[obj->fireLightIndex].vulkanLight.position = lightPos;
                m_SceneLights[obj->fireLightIndex].vulkanLight.intensity = targetIntensity * flicker;
            }

            // Transition to Burnt (Ash)
            if (obj->burnTimer >= obj->maxBurnDuration) {
                obj->state = ObjectState::BURNT;

                // Stop Fire
                if (obj->fireEmitterId != -1) {
                    GetOrCreateSystem(ParticleLibrary::GetFireProps())->StopEmitter(obj->fireEmitterId);
                    obj->fireEmitterId = -1;
                }
                // Stop Light
                if (obj->fireLightIndex != -1 && obj->fireLightIndex < m_SceneLights.size()) {
                    m_SceneLights[obj->fireLightIndex].vulkanLight.intensity = 0.0f;
                }
                // Switch Smoke to Smoldering
                if (obj->smokeEmitterId != -1) {
                    ParticleProps smolder = ParticleLibrary::GetSmokeProps();
                    smolder.position = basePos;
                    smolder.sizeBegin *= 0.1f;
                    smolder.sizeEnd *= 0.2f;
                    smolder.lifeTime = 1.5f;
                    smolder.velocity.y = 0.5f;
                    smolder.positionVariation = glm::vec3(0.1f);
                    GetOrCreateSystem(smolder)->UpdateEmitter(obj->smokeEmitterId, smolder, 20.0f);
                }

                // Swap Geometry
                obj->storedOriginalGeometry = obj->geometry;
                obj->storedOriginalTransform = obj->transform;

                if (dustGeometryPrototype) {
                    obj->geometry = dustGeometryPrototype;
                }
                obj->texturePath = sootTexturePath;

                // Shrink
                obj->transform = glm::translate(glm::mat4(1.0f), basePos);
                obj->transform = glm::scale(obj->transform, glm::vec3(0.003f));

                obj->regrowTimer = 0.0f;
                obj->burnFactor = 0.0f;
            }
            break;
        }

        case ObjectState::BURNT:
        case ObjectState::REGROWING: {
            // Stable Cooling towards ambient
            const float changeRate = 0.5f * deltaTime;
            const float lerpFactor = glm::clamp(changeRate, 0.0f, 1.0f);
            obj->currentTemp = glm::mix(obj->currentTemp, m_WeatherIntensity, lerpFactor);

            // --- Dynamic Growth Logic ---
            // Standard rate at 25C. Colder = Slower. Below 10C = No Growth.
            float growthMultiplier = 0.0f;
            if (m_WeatherIntensity > 10.0f) {
                growthMultiplier = (m_WeatherIntensity - 10.0f) / 15.0f;
            }
            obj->regrowTimer += deltaTime * growthMultiplier;

            // Stop smoldering after 5s (biological/active time)
            if (obj->state == ObjectState::BURNT && obj->regrowTimer > 5.0f && obj->smokeEmitterId != -1) {
                GetOrCreateSystem(ParticleLibrary::GetSmokeProps())->StopEmitter(obj->smokeEmitterId);
                obj->smokeEmitterId = -1;
            }

            if (obj->state == ObjectState::BURNT) {
                // --- Short wait (5s) for ash to settle ---
                if (obj->regrowTimer >= 10.0f) {
                    obj->state = ObjectState::REGROWING;
                    obj->regrowTimer = 0.0f;

                    // Reset temp immediately so it doesn't loop back to burning
                    obj->currentTemp = m_WeatherIntensity;

                    if (obj->storedOriginalGeometry) {
                        obj->geometry = obj->storedOriginalGeometry;
                        obj->storedOriginalGeometry = nullptr;
                    }
                    obj->texturePath = obj->originalTexturePath;
                }
            }
            else if (obj->state == ObjectState::REGROWING) {
                // --- Gradual Growth takes 0.75 of a day ---
                const float growthTime = m_TimeConfig.dayLengthSeconds * 0.75f;

                float t = glm::clamp(obj->regrowTimer / growthTime, 0.0f, 1.0f);
                t = t * t * (3.0f - 2.0f * t); // Smoothstep curve

                const float currentScale = glm::mix(0.003f, 1.0f, t);
                obj->transform = glm::scale(obj->storedOriginalTransform, glm::vec3(currentScale));

                if (t >= 1.0f) {
                    obj->state = ObjectState::NORMAL;
                    obj->currentTemp = m_WeatherIntensity;
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
    // 1. Reset Lights (Sun/Moon Orbits)
    for (auto& light : m_SceneLights) {
        if (light.OrbitData.isOrbiting) {
            light.OrbitData.currentAngle = light.OrbitData.initialAngle;
            // Recalculate position immediately
            const glm::quat rotation = glm::angleAxis(light.OrbitData.currentAngle, light.OrbitData.axis);
            const glm::vec3 offset = rotation * light.OrbitData.startVector;
            light.vulkanLight.position = light.OrbitData.center + offset;
        }
    }

    // 2. Reset Objects
    for (auto& obj : objects) {
        // A. Reset Orbit
        if (obj->OrbitData.isOrbiting) {
            obj->OrbitData.currentAngle = obj->OrbitData.initialAngle;
            const glm::quat rotation = glm::angleAxis(obj->OrbitData.currentAngle, obj->OrbitData.axis);
            const glm::vec3 offset = rotation * obj->OrbitData.startVector;
            obj->transform[3] = glm::vec4(obj->OrbitData.center + offset, 1.0f);
        }

        // B. Reset Thermodynamics / Visual State
        if (obj->isFlammable) {
            StopObjectFire(obj.get());

            // Restore Geometry if it was swapped (Burnt state)
            if (obj->storedOriginalGeometry) {
                obj->geometry = obj->storedOriginalGeometry;
                obj->storedOriginalGeometry = nullptr;
                obj->transform = obj->storedOriginalTransform; // Restore original scale/transform
            }
            else if (obj->state == ObjectState::REGROWING) {
                // If it was regrowing, it might be using original geometry but scaled down
                obj->transform = obj->storedOriginalTransform;
            }

            // Restore Texture
            obj->texturePath = obj->originalTexturePath;

            // Reset Variables
            obj->state = ObjectState::NORMAL;
            obj->currentTemp = 0.0f;
            obj->burnTimer = 0.0f;
            obj->regrowTimer = 0.0f;
            obj->burnFactor = 0.0f;
        }
    }
    StopPrecipitation();
    m_IsPrecipitating = false;
    m_WeatherTimer = 0.0f;

    // Reset to a random clear interval
    PickNextWeatherDuration();

    StopDust();
    m_TimeSinceLastRain = 0.0f;
}