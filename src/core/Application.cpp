#include "Application.h"
#include "../rendering/ParticleLibrary.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <thread>
#include <limits>
#include <cctype>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "../geometry/GeometryGenerator.h"
#include "../geometry/OBJLoader.h"
#include "../geometry/SJGLoader.h"
#include "../systems/CameraSystem.h"
#include "InputManager.h"
#include "Config.h"
#include "ClothFactory.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/ObjectSpawnerSystem.h"

namespace {
    constexpr bool kSceneDebug = false;
    constexpr bool kPerfDebug = false;
    constexpr float kPerfLogIntervalSeconds = 1.0f;
    constexpr float kPerfHitchThresholdMs = 20.0f;
}

// TODO:
// refactor and decouple scene class to entity component system
// refector scene object to seperate Transform, Rendering, Physics, Thermodynamics, Orbital
// specific pass members for renderer
// 
// more runtime environmental control / debugging
// input manager class
// debug class with console output and imgui integration
// audio engine
// wind + fire spread
// bump, displacement and normal mapping
// deferred rendering pipeline (MRT)
// high dynamic range rendering (HDR)
// illuminating sparks
// ray tracing
// Compute Shaders for particles
// Shadow mapping improvements (PCF, VSM, CSM)

Application::Application() {
    //config = ConfigLoader::Load("src/config/collisions/");

    window = std::make_unique<Window>(config.windowWidth, config.windowHeight, "VulkanPhysics");

    // Setup ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(window->GetGLFWWindow(), true);

    glfwSetWindowUserPointer(window->GetGLFWWindow(), this);
    glfwSetKeyCallback(window->GetGLFWWindow(), KeyCallback);
    glfwSetFramebufferSizeCallback(window->GetGLFWWindow(), FramebufferResizeCallback);
}

void Application::Run() {
    InitVulkan();

    std::string initialPath = editorUI->GetInitialScenePath();
    if (!initialPath.empty()) {
        LoadScene(initialPath);
    }

    lastFrameTime = std::chrono::high_resolution_clock::now();

    MainLoop();
    Cleanup();
}

void Application::InitVulkan() {
    vulkanContext = std::make_unique<VulkanContext>();
    vulkanContext->CreateInstance();
    vulkanContext->SetupDebugMessenger();
    vulkanContext->CreateSurface(window->GetGLFWWindow());

    vulkanDevice = std::make_unique<VulkanDevice>(
        vulkanContext->GetInstance(),
        vulkanContext->GetSurface()
    );
    vulkanDevice->PickPhysicalDevice();
    vulkanDevice->CreateLogicalDevice();

    vulkanSwapChain = std::make_unique<VulkanSwapChain>(
        vulkanDevice->GetDevice(),
        vulkanDevice->GetPhysicalDevice(),
        vulkanContext->GetSurface(),
        window->GetGLFWWindow(),
        config.vsync
    );
    vulkanSwapChain->Create(vulkanDevice->GetQueueFamilies());
    vulkanSwapChain->CreateImageViews();

    renderer = std::make_unique<Renderer>(
        vulkanDevice.get(),
        vulkanSwapChain.get()
    );
    renderer->Initialize();

    inputManager = std::make_unique<InputManager>();

    scene = std::make_unique<Scene>(
        vulkanDevice->GetDevice(),
        vulkanDevice->GetPhysicalDevice()
    );

    try {
        scene->Initialize();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    renderer->SetupSceneParticles(*scene);

    cameraController = std::make_unique<CameraController>(*scene, config.customCameras);

    std::vector<std::string> camNames;

    // 1. Initialize UI and find the "init" index
    editorUI = std::make_unique<EditorUI>();
    editorUI->Initialize("src/worlds/", "cloth_test");
    editorUI->SetPerformanceSettings(config.vsync, config.maxFps);

    for (const auto& cam : config.customCameras) {
        camNames.push_back(cam.name);
    }
    editorUI->SetAvailableCameras(camNames);


}

void Application::LoadScene(const std::string& scenePath) {
    // 1. Wait for GPU to finish current frames
    if (vulkanDevice) {
        vkDeviceWaitIdle(vulkanDevice->GetDevice());
    }

    // 2. Clear current scene data
    if (scene) {
        scene->Clear();
    }

    // 3. Load new configuration
    config = ConfigLoader::Load(scenePath);
    currentScenePath = scenePath;
    editorUI->SetPerformanceSettings(config.vsync, config.maxFps);

    if (vulkanSwapChain && vulkanSwapChain->IsVSyncEnabled() != config.vsync) {
        vulkanSwapChain->SetVSyncEnabled(config.vsync);
        RecreateSwapChain();
    }
    auto activeBindings = inputManager->LoadFromBindings(config.inputBindings);
    editorUI->SetInputBindings(activeBindings);

    // 4. Re-setup scene objects
    try {
        SetupScene();
        ObjectSpawnerSystem::TriggerStartupSpawners(*scene);
    }
    catch (const std::exception& e) {
        std::cerr << "[LoadScene] SetupScene failed for '" << scenePath << "' with error: " << e.what() << std::endl;
        throw;
    }
    catch (...) {
        std::cerr << "[LoadScene] SetupScene failed for '" << scenePath << "' with unknown error." << std::endl;
        throw;
    }

    // 5. Re-initialize systems that depend on the new config
    cameraController = std::make_unique<CameraController>(*scene, config.customCameras);
    std::vector<std::string> camNames;
    for (const auto& cam : config.customCameras) {
        camNames.push_back(cam.name);
    }
    editorUI->SetAvailableCameras(camNames);


    if (renderer && scene) {
        renderer->SetupSceneParticles(*scene);
    }

    if (kSceneDebug) {
        std::cout << "Loaded Scene: " << scenePath << std::endl;
    }
}

void Application::ReloadCurrentScene() {
    if (currentScenePath.empty()) {
        return;
    }

    LoadScene(currentScenePath);
}


void Application::SetupScene() {
    // Reset layer globals per-scene
    SceneLayers::ActiveLayerCount = 1;
    SceneLayers::LayerNames[0] = "Base World";
    SceneLayers::LayerNames[1] = "Layer B";
    SceneLayers::LayerNames[2] = "Layer C";
    SceneLayers::LayerNames[3] = "Layer D";
    SceneLayers::LayerNames[4] = "Layer E";
    SceneLayers::LayerNames[5] = "Layer F";
    SceneLayers::LayerNames[6] = "Layer G";
    SceneLayers::LayerNames[7] = "Layer H";

    int envThermoPolicyMode = 0;
    std::vector<std::string> envThermoPolicyEntities;

    // 1. Pass Global Configuration to Scene
    for (const auto& objCfg : config.sceneObjects) {
        if (objCfg.type == "Environment") {
            scene->CreateEnvironment(objCfg.name);
            scene->SetTimeConfig(objCfg.timeConfig);
            scene->SetSeasonConfig(objCfg.seasonConfig);
            scene->SetWeatherConfig(objCfg.weatherConfig);
            scene->SetSunHeatBonus(objCfg.sunHeatBonus);

            envThermoPolicyMode = objCfg.thermoPolicyMode;
            envThermoPolicyEntities = objCfg.thermoPolicyEntities;
            break;
        }
    }

    // --- GENERATE PROCEDURAL TEXTURES ---
    for (const auto& texCfg : config.proceduralTextures) {
        if (texCfg.type == "Checker") {
            renderer->RegisterProceduralTexture(texCfg.name, [texCfg](Texture& tex) {
                tex.GenerateCheckerboard(texCfg.width, texCfg.height, texCfg.color1, texCfg.color2, texCfg.cellSize);
                });
        }
        else if (texCfg.type == "Gradient") {
            renderer->RegisterProceduralTexture(texCfg.name, [texCfg](Texture& tex) {
                tex.GenerateGradient(texCfg.width, texCfg.height, texCfg.color1, texCfg.color2, texCfg.isVertical);
                });
        }
        else if (texCfg.type == "Solid") {
            renderer->RegisterProceduralTexture(texCfg.name, [texCfg](Texture& tex) {
                tex.GenerateSolidColor(texCfg.color1);
                });
        }
        if (kSceneDebug) {
            std::cout << "Generated Texture: " << texCfg.name << " (" << texCfg.type << ")" << std::endl;
        }
    }

    // 2. Setup Procedural Objects (Vegetation)
    // These are currently still generated randomly, but defined in config
    scene->ClearProceduralRegistry();
    if (config.proceduralPlants.empty()) {
        // Safe defaults if config file is missing
        //scene->RegisterProceduralObject("models/cactus.obj", "textures/cactus.jpg", 7.0f, glm::vec3(0.01f), glm::vec3(0.02f), glm::vec3(-90.0f, 0.0f, 0.0f), true);
    }
    else {
        for (const auto& plant : config.proceduralPlants) {
            scene->RegisterProceduralObject(plant.modelPath, plant.texturePath, plant.frequency, plant.minScale, plant.maxScale, plant.baseRotation, plant.isFlammable);
        }
    }

    // Capture Terrain Params for procedural generation later
    // Defaults (hard coded for desert world):
    float terrainRadius = 150.0f;
    float terrainY = -75.0f;
    float heightScale = 3.5f;
    float noiseFreq = 0.02f;

    // 3. Process Explicit Scene Objects
    for (const auto& objCfg : config.sceneObjects) {
        std::string setupPhase = "Begin";
        if (kSceneDebug) {
            std::cout << "[SetupScene] Object Start: '" << objCfg.name << "' type='" << objCfg.type << "'" << std::endl;
        }

        try {

        // --- Geometry Creation ---
        setupPhase = "Geometry Creation";
        if (objCfg.type == "Terrain") {
            // Params: x=Radius, y=HeightScale, z=NoiseFreq
            scene->AddTerrain(objCfg.name, objCfg.params.x, 256, 256, objCfg.params.y, objCfg.params.z, objCfg.position, objCfg.texturePath);

            // Update procedural generation targets
            terrainRadius = objCfg.params.x;
            heightScale = objCfg.params.y;
            noiseFreq = objCfg.params.z;
            terrainY = objCfg.position.y;
        }
        else if (objCfg.type == "Pedestal") {
            // Params: x=TopRadius, y=BaseWidth, z=Height
            scene->AddPedestal(objCfg.name, objCfg.params.x, objCfg.params.y, objCfg.params.z, objCfg.position, objCfg.texturePath);
        }
        else if (objCfg.type == "Sphere") {
            // Params: x=Radius
            scene->AddSphere(objCfg.name, 12, 24, objCfg.params.x, objCfg.position, objCfg.texturePath);
        }
        else if (objCfg.type == "Bowl") {
            // Params: x=Radius
            scene->AddBowl(objCfg.name, objCfg.params.x, 24, 12, objCfg.position, objCfg.texturePath);
        }
        else if (objCfg.type == "Cube") {
            scene->AddCube(objCfg.name, objCfg.position, objCfg.scale, objCfg.texturePath);
        }
        else if (objCfg.type == "Model") {
            // Standard Model
            scene->AddModel(objCfg.name, objCfg.position, objCfg.rotation, objCfg.scale, objCfg.modelPath, objCfg.texturePath, objCfg.isFlammable);
        }
        else if (objCfg.type == "Grid") {
            // Params: x=Rows, y=Cols, z=CellSize
            scene->AddGrid(objCfg.name, (int)objCfg.params.x, (int)objCfg.params.y, objCfg.params.z, objCfg.position, objCfg.texturePath);
        }
        else if (objCfg.type == "Cloth") {
            setupPhase = "Cloth Creation";
            float stiffness = objCfg.hasSpringConfig ? objCfg.springStiffness : 15.0f;
            float damping = objCfg.hasSpringConfig ? objCfg.springDamping : 0.5f;
            
            Entity clothEntity = ClothFactory::CreateClothGrid(
                *scene, vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(),
                objCfg.position, (int)objCfg.params.x, (int)objCfg.params.y, objCfg.params.z,
                objCfg.mass, stiffness, damping, objCfg.texturePath
            );
            
            scene->RegisterEntityName(objCfg.name, clothEntity);
        }
        else if (objCfg.type == "Spawner") {
            setupPhase = "Spawner Initialization";
            Entity spawnerEntity = scene->CreateSpawnerEntity(objCfg.name, objCfg.position);

            ObjectSpawnerComponent spawner;
            spawner.alwaysOn = objCfg.spawnerEnabled;
            spawner.isRunning = objCfg.spawnerEnabled || objCfg.spawnerTriggerOnStartup;
            spawner.triggerOnStartup = objCfg.spawnerTriggerOnStartup;
            spawner.group = objCfg.spawnerGroup.empty()
                ? 'A'
                : static_cast<char>(std::toupper(static_cast<unsigned char>(objCfg.spawnerGroup[0])));
            if (spawner.group < 'A' || spawner.group > 'D') {
                spawner.group = 'A';
            }
            spawner.spawnInterval = objCfg.spawnInterval;
            spawner.runDurationSeconds = objCfg.spawnerRunDurationSeconds;
            spawner.maxSpawnsPerRun = objCfg.spawnerMaxSpawnsPerRun;
            spawner.spawnGeometryType = objCfg.spawnGeometryType;
            spawner.spawnModelPath = objCfg.spawnModelPath;
            spawner.spawnTexturePath = objCfg.spawnTexturePath.empty() ? objCfg.texturePath : objCfg.spawnTexturePath;
            spawner.spawnScale = objCfg.spawnScale;
            spawner.spawnObjectScale = std::max(0.05f, std::max({ objCfg.spawnScale.x, objCfg.spawnScale.y, objCfg.spawnScale.z }));
            spawner.spawnVelocity = objCfg.spawnVelocity;
            spawner.randomizeVelocity = objCfg.randomizeSpawnVelocity;
            spawner.randomVelocityRange = objCfg.spawnVelocityRandomRange;
            // NEW: angular velocity config
            spawner.spawnAngularVelocity = objCfg.spawnAngularVelocity;
            spawner.randomizeAngularVelocity = objCfg.randomizeSpawnAngularVelocity;
            spawner.randomAngularVelocityRange = objCfg.spawnAngularVelocityRandomRange;
            spawner.spawnMass = objCfg.spawnMass;
            spawner.spawnLifespanSeconds = objCfg.spawnLifespanSeconds;
            spawner.attachToTarget = objCfg.spawnerAttachToTarget;
            spawner.attachTargetName = objCfg.spawnerTargetName;

            if (spawner.alwaysOn) {
                spawner.runDurationSeconds = -1.0f;
                spawner.maxSpawnsPerRun = -1;
            }

            scene->GetRegistry().AddComponent<ObjectSpawnerComponent>(spawnerEntity, spawner);
            if (kSceneDebug) {
                std::cout << "[SetupScene] Object Success: '" << objCfg.name << "' (Spawner)" << std::endl;
            }
            continue;
        }
        if (objCfg.type == "Environment") {
            if (kSceneDebug) {
                std::cout << "[SetupScene] Object Skip: '" << objCfg.name << "' (Environment handled earlier)" << std::endl;
            }
            continue;
        }
        else if (objCfg.type == "DustCloud") {
            setupPhase = "DustCloud Creation";
            scene->CreateDustCloud(objCfg.name, objCfg.position, objCfg.direction, objCfg.speed, objCfg.isActive);
        }

        // --- Apply Common Properties ---
        setupPhase = "Apply Common Properties";
        // (We assume the object was just added to the back of the vector)
        scene->SetObjectTransform(objCfg.name, objCfg.position, objCfg.rotation, objCfg.scale);
        scene->SetObjectVisible(objCfg.name, objCfg.visible);
        scene->SetObjectCastsShadow(objCfg.name, objCfg.castsShadow);
        scene->SetObjectReceivesShadows(objCfg.name, objCfg.receiveShadows);
        scene->SetObjectShadingMode(objCfg.name, objCfg.shadingMode);
        scene->SetObjectLayerMask(objCfg.name, objCfg.layerMask);
        scene->SetObjectRegionVisibilityMasks(objCfg.name, objCfg.onlyInRegionMask);

        auto& registry = scene->GetRegistry();
        Entity entity = scene->GetEntityByName(objCfg.name);

        if (entity != MAX_ENTITIES) {
            setupPhase = "Attach Optional Components";
            if (objCfg.hasPhysicsConfig && !registry.HasComponent<PhysicsComponent>(entity)) {
                registry.AddComponent<PhysicsComponent>(entity, PhysicsComponent{});
            }

            if (objCfg.hasColliderConfig && !registry.HasComponent<ColliderComponent>(entity)) {
                registry.AddComponent<ColliderComponent>(entity, ColliderComponent{});
            }

            bool thermoEnabled = false;
            if (objCfg.hasThermoOverride) {
                thermoEnabled = objCfg.thermoEnabled;
            }
            else {
                if (envThermoPolicyMode == 0) {
                    thermoEnabled = objCfg.isFlammable;
                }
                else {
                    const bool listed = std::find(envThermoPolicyEntities.begin(), envThermoPolicyEntities.end(), objCfg.name) != envThermoPolicyEntities.end();
                    if (envThermoPolicyMode == 1) {
                        thermoEnabled = !listed;
                    }
                    else if (envThermoPolicyMode == 2) {
                        thermoEnabled = listed;
                    }
                }
            }

            if (thermoEnabled && !registry.HasComponent<ThermoComponent>(entity)) {
                ThermoComponent thermo;
                thermo.isFlammable = objCfg.isFlammable;
                registry.AddComponent<ThermoComponent>(entity, thermo);
            }
            else if (!thermoEnabled && registry.HasComponent<ThermoComponent>(entity)) {
                registry.RemoveComponent<ThermoComponent>(entity);
            }
            else if (thermoEnabled && registry.HasComponent<ThermoComponent>(entity)) {
                registry.GetComponent<ThermoComponent>(entity).isFlammable = objCfg.isFlammable;
            }
        }

        if (objCfg.hasColliderConfig) {
            setupPhase = "Configure Collider";
            scene->SetObjectCollision(objCfg.name, objCfg.hasCollision);
            scene->SetObjectCollider(objCfg.name, objCfg.colliderType, objCfg.colliderRadius, objCfg.colliderNormal);
        }

        if (objCfg.hasPhysicsConfig) {
            setupPhase = "Configure Physics";
            scene->SetObjectPhysics(objCfg.name, objCfg.isStatic, objCfg.mass);
            scene->SetObjectPhysicsMaterial(objCfg.name, objCfg.restitution, objCfg.friction);
        }
        // --- Apply Light ---

        float collisionRadius = objCfg.colliderRadius;
        float collisionHeight = objCfg.colliderHeight;

        // Auto-fit finite plane collider extents to Grid geometry dimensions.
        if (objCfg.type == "Grid" && objCfg.colliderType == 1) {
            const float gridWidth = std::max(0.0f, objCfg.params.y) * std::max(0.0f, objCfg.params.z);
            const float gridDepth = std::max(0.0f, objCfg.params.x) * std::max(0.0f, objCfg.params.z);

            collisionRadius = 0.5f * gridWidth * std::abs(objCfg.scale.x);
            collisionHeight = 0.5f * gridDepth * std::abs(objCfg.scale.z);
        }

        if (objCfg.hasColliderConfig) {
            setupPhase = "Configure Collision Size";
            scene->SetObjectCollisionSize(objCfg.name, collisionRadius, collisionHeight);
        }

        if (objCfg.isDespawner) {
            setupPhase = "Attach Despawner";
            Entity entity = scene->GetEntityByName(objCfg.name);
            if (entity != MAX_ENTITIES && !scene->GetRegistry().HasComponent<DespawnerComponent>(entity)) {
                scene->GetRegistry().AddComponent<DespawnerComponent>(entity, DespawnerComponent{});
            }
        }

        if (objCfg.isLight) {
            setupPhase = "Configure Light";
            scene->AddLight(objCfg.name, objCfg.position, objCfg.lightColor, objCfg.lightIntensity, objCfg.lightType);
            scene->SetLightLayerMask(objCfg.name, objCfg.layerMask);

            bool flickerEnabled = objCfg.lightFlickerEnabled;
            float flickerAmount = objCfg.lightFlickerAmount;
            int flickerPreset = objCfg.lightFlickerPreset;

            if (objCfg.lightType == 1 && !objCfg.hasExplicitLightFlicker) {
                flickerEnabled = true;
                flickerAmount = 0.65f;
                flickerPreset = 1; // Fire
            }

            scene->SetLightFlicker(objCfg.name, flickerEnabled, flickerAmount, flickerPreset);
        }

        // --- Apply Orbit ---
        if (objCfg.hasOrbit) {
            setupPhase = "Configure Orbit";
            // Calculate Axis from "Direction Degrees"
            auto GetTrajectory = [](float degrees) -> glm::vec3 {
                const float rad = glm::radians(degrees);
                glm::vec3 t(cos(rad), 0.0f, sin(rad));
                if (glm::length(t) < 0.001f) t = glm::vec3(1.0f, 0.0f, 0.0f);
                return glm::normalize(t);
                };

            const glm::vec3 trajectory = GetTrajectory(objCfg.orbitDirection);
            // Default "Up" for orbit cross product is Y-up
            const glm::vec3 axis = glm::normalize(glm::cross(trajectory, glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 startVector = trajectory * objCfg.orbitRadius;

            // Determine Speed
            float speed = objCfg.orbitSpeed;
            if (speed < -0.001f) {
                // Auto-calculate based on Day Length
                const float dayLength = scene->GetTimeConfig().dayLengthSeconds;
                speed = (dayLength > 0.0f) ? (glm::two_pi<float>() / dayLength) : 0.1f;
            }

            // Apply to Object
            scene->SetObjectOrbit(objCfg.name, objCfg.position, objCfg.orbitRadius, speed, axis, startVector, objCfg.orbitInitialAngle);

            // Apply to Light (if it exists)
            if (objCfg.isLight) {
                scene->SetLightOrbit(objCfg.name, objCfg.position, objCfg.orbitRadius, speed, axis, startVector, objCfg.orbitInitialAngle);
            }
        }

        // --- Apply Springs ---
        if (objCfg.hasSpringConfig) {
            setupPhase = "Configure Springs";
            Entity anchorEnt = scene->GetEntityByName(objCfg.name);
            
            if (anchorEnt != MAX_ENTITIES) {
                SpringComponent spring;
                spring.isAttachedToEntity = true;
                spring.restingLength = objCfg.springRestingLength;
                spring.stiffness = objCfg.springStiffness;
                spring.damping = objCfg.springDamping;

                for (const auto& targetName : objCfg.springConnections) {
                    Entity targetEnt = scene->GetEntityByName(targetName);
                    if (targetEnt != MAX_ENTITIES) {
                        spring.connectedEntities.push_back(targetEnt);
                    } else {
                        std::cerr << "Warning: Spring target '" << targetName << "' not found for anchor '" << objCfg.name << "'. Ensure the child object is defined BEFORE the anchor in the .world file.\n";
                    }
                }
                
                scene->GetRegistry().AddComponent<SpringComponent>(anchorEnt, spring);
            }
        }

        if (objCfg.hasPathAnimation && entity != MAX_ENTITIES) {
            auto toUpper = [](std::string value) {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                return value;
                };

            auto parsePlayMode = [&](const std::string& mode) {
                const std::string value = toUpper(mode);
                if (value == "LOOP") return PathAnimationPlayMode::Loop;
                if (value == "BOUNCE") return PathAnimationPlayMode::Bounce;
                return PathAnimationPlayMode::Once;
                };

            auto parseEasing = [&](const std::string& mode) {
                const std::string value = toUpper(mode);
                if (value == "SMOOTHSTEP" || value == "SMOOTH") return PathAnimationEasing::Smoothstep;
                return PathAnimationEasing::Linear;
                };

            auto parseTimingMode = [&](const std::string& mode) {
                const std::string value = toUpper(mode);
                if (value == "PER_SEGMENT") return PathAnimationTimingMode::PerSegment;
                if (value == "OVERALL_TIME") return PathAnimationTimingMode::OverallTime;
                return PathAnimationTimingMode::Absolute;
                };

            auto parseCurveType = [&](const std::string& mode) {
                const std::string value = toUpper(mode);
                if (value == "BEZIER_QUADRATIC") return PathCurveType::BezierQuadratic;
                return PathCurveType::Straight;
                };

            PathAnimationComponent pathAnim;
            pathAnim.playMode = parsePlayMode(objCfg.pathPlayMode);
            pathAnim.timingMode = parseTimingMode(objCfg.pathTimingMode);
            pathAnim.easing = parseEasing(objCfg.pathEasing);
            pathAnim.totalDuration = objCfg.pathTotalDuration;
            pathAnim.isPlaying = objCfg.pathIsPlaying;
            pathAnim.showPath = objCfg.pathShowPath;
            pathAnim.useLocalSpace = objCfg.pathUseLocalSpace;
            pathAnim.connectEndToStart = objCfg.pathConnectEndToStart;
            pathAnim.reversePath = (toUpper(objCfg.pathPlayMode) == "REVERSE");
            pathAnim.initialized = false;
            pathAnim.currentTime = 0.0f;
            pathAnim.playbackDirection = 1;
            pathAnim.lastReversePath = pathAnim.reversePath;
            pathAnim.hasLastEvaluatedPosition = false;

            auto syncPathSegments = [](PathAnimationComponent& path) {
                const size_t targetSegmentCount = (path.connectEndToStart && path.waypoints.size() > 1)
                    ? path.waypoints.size()
                    : (path.waypoints.size() > 0 ? path.waypoints.size() - 1 : 0);

                while (path.segments.size() < targetSegmentCount) {
                    PathCurveSegment segment;
                    const size_t segmentIndex = path.segments.size();
                    const size_t startIndex = segmentIndex;
                    const size_t endIndex = (path.connectEndToStart && segmentIndex + 1 == path.waypoints.size()) ? 0 : segmentIndex + 1;
                    if (startIndex < path.waypoints.size() && endIndex < path.waypoints.size()) {
                        const glm::vec3 start = path.waypoints[startIndex].position;
                        const glm::vec3 end = path.waypoints[endIndex].position;
                        segment.controlPoint = (start + end) * 0.5f;
                    }
                    path.segments.push_back(segment);
                }

                if (path.segments.size() > targetSegmentCount) {
                    path.segments.resize(targetSegmentCount);
                }
            };

            if (!objCfg.pathWaypoints.empty()) {
                for (const auto& waypointCfg : objCfg.pathWaypoints) {
                    PathWaypoint waypoint;
                    waypoint.position = waypointCfg.position;
                    waypoint.orientation = waypointCfg.orientation;
                    waypoint.timeFromStart = std::max(0.0f, waypointCfg.timeFromStart);
                    pathAnim.waypoints.push_back(waypoint);
                }

                for (const auto& segCfg : objCfg.pathSegments) {
                    PathCurveSegment segment;
                    segment.curveType = parseCurveType(segCfg.curveType);
                    segment.controlPoint = segCfg.controlPoint;
                    segment.duration = std::max(0.001f, segCfg.duration);
                    pathAnim.segments.push_back(segment);
                }

                if (!pathAnim.waypoints.empty()) {
                    while (pathAnim.segments.size() + 1 < pathAnim.waypoints.size()) {
                        PathCurveSegment segment;
                        const size_t segmentIndex = pathAnim.segments.size();
                        const glm::vec3 start = pathAnim.waypoints[segmentIndex].position;
                        const glm::vec3 end = pathAnim.waypoints[segmentIndex + 1].position;
                        segment.controlPoint = (start + end) * 0.5f;
                        pathAnim.segments.push_back(segment);
                    }
                    if (pathAnim.segments.size() >= pathAnim.waypoints.size()) {
                        pathAnim.segments.resize(pathAnim.waypoints.size() - 1);
                    }
                }

                syncPathSegments(pathAnim);
            }
            else if (!objCfg.legacyPathSegments.empty()) {
                float accumulatedTime = 0.0f;
                bool useOverallLegacyTime = (toUpper(objCfg.pathTimingMode) == "OVERALL_TIME");
                float totalLength = 0.0f;

                if (useOverallLegacyTime) {
                    for (const auto& segCfg : objCfg.legacyPathSegments) {
                        totalLength += glm::length(segCfg.endPoint - segCfg.startPoint);
                    }
                }

                for (size_t i = 0; i < objCfg.legacyPathSegments.size(); ++i) {
                    const auto& segCfg = objCfg.legacyPathSegments[i];
                    if (i == 0) {
                        PathWaypoint first;
                        first.position = segCfg.startPoint;
                        first.orientation = objCfg.rotation;
                        first.timeFromStart = 0.0f;
                        pathAnim.waypoints.push_back(first);
                    }

                    float segmentDuration = std::max(0.001f, segCfg.duration);
                    if (useOverallLegacyTime) {
                        const float segmentLength = glm::length(segCfg.endPoint - segCfg.startPoint);
                        if (totalLength > 0.0001f) {
                            segmentDuration = std::max(0.001f, objCfg.pathTotalDuration * (segmentLength / totalLength));
                        }
                        else {
                            segmentDuration = std::max(0.001f, objCfg.pathTotalDuration / static_cast<float>(std::max<size_t>(1, objCfg.legacyPathSegments.size())));
                        }
                    }

                    accumulatedTime += segmentDuration;

                    PathWaypoint waypoint;
                    waypoint.position = segCfg.endPoint;
                    waypoint.orientation = objCfg.rotation;
                    waypoint.timeFromStart = accumulatedTime;
                    pathAnim.waypoints.push_back(waypoint);

                    PathCurveSegment curveSegment;
                    curveSegment.curveType = parseCurveType(segCfg.curveType);
                    curveSegment.controlPoint = segCfg.controlPoint;
                    curveSegment.duration = segmentDuration;
                    pathAnim.segments.push_back(curveSegment);
                }

                if (objCfg.pathTotalDuration <= 0.0f) {
                    pathAnim.totalDuration = accumulatedTime;
                }

                syncPathSegments(pathAnim);
            }

            if (pathAnim.waypoints.size() == 1) {
                pathAnim.totalDuration = std::max(pathAnim.totalDuration, pathAnim.waypoints.front().timeFromStart);
            }

            if (registry.HasComponent<PathAnimationComponent>(entity)) {
                registry.GetComponent<PathAnimationComponent>(entity) = pathAnim;
            }
            else {
                registry.AddComponent<PathAnimationComponent>(entity, pathAnim);
            }

            if (!registry.HasComponent<PhysicsComponent>(entity)) {
                registry.AddComponent<PhysicsComponent>(entity, PhysicsComponent{});
            }
            auto& pathPhysics = registry.GetComponent<PhysicsComponent>(entity);
            pathPhysics.isStatic = true;
            pathPhysics.SetMass(0.0f);
            pathPhysics.velocity = glm::vec3(0.0f);
        }

        if (kSceneDebug) {
            std::cout << "[SetupScene] Object Success: '" << objCfg.name << "'" << std::endl;
        }
        }
        catch (const std::exception& e) {
            std::cerr << "[SetupScene] Object Failed: '" << objCfg.name << "' type='" << objCfg.type
                << "' phase='" << setupPhase << "' error='" << e.what() << "'" << std::endl;
            throw;
        }
        catch (...) {
            std::cerr << "[SetupScene] Object Failed: '" << objCfg.name << "' type='" << objCfg.type
                << "' phase='" << setupPhase << "' unknown error" << std::endl;
            throw;
        }
    }

    if (config.enableDefaultDeathWall) {
        const float deathWallY = terrainY - 100.0f;
        scene->CreateDeathWall("__DefaultDeathWall", deathWallY, 5000.0f, 5000.0f);
    }

    for (const auto& regionCfg : config.layerRegions) {
        scene->AddLayerRegion(
            regionCfg.name,
            regionCfg.assignedLayerBit,
            regionCfg.volumeType,
            regionCfg.radius,
            regionCfg.halfExtents,
            regionCfg.position
        );
        if (kSceneDebug) {
            std::cout << "Loaded Layer Region: " << regionCfg.name << " (Bit: " << regionCfg.assignedLayerBit << ")" << std::endl;
        }
    }

    // 4. Generate Procedural Vegetation
    // We use the terrainRadius we captured (minus buffer) to ensure plants spawn on the terrain
    if (!config.proceduralPlants.empty()) {
        scene->GenerateProceduralObjects(config.proceduralObjectCount, terrainRadius - 20.0f, terrainY, heightScale, noiseFreq);
    }
    
    // Spawn Cloth Demo
    // ClothFactory::CreateClothGrid(*scene, vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), glm::vec3(0.0f, 30.0f, 0.0f), 15, 15, 1.0f, 0.5f, 15.0f, 0.5f);

    // scene->PrintDebugInfo();
}

void Application::RecreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window->GetGLFWWindow(), &width, &height);

    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window->GetGLFWWindow(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vulkanDevice->GetDevice());

    renderer->CleanupSwapChainResources();
    vulkanSwapChain->Cleanup();

    vulkanSwapChain->Create(vulkanDevice->GetQueueFamilies());
    vulkanSwapChain->CreateImageViews();

    renderer->RecreateSwapChainResources();

    framebufferResized = false;
}

void Application::GenerateLookahead(float timeframe) {
    if (!scene) return;

    std::cout << "Generating lookahead for " << timeframe << " seconds..." << std::endl;
    m_ReplayFrames.clear();

    const float stepTime = 1.0f / 60.0f;
    const int steps = static_cast<int>(timeframe / stepTime);

    // Save original state to restore if needed, but since we are applying the lookahead,
    // the user will scrub back if they want the past.
    scene->SetLookaheadMode(true);

    for (int i = 0; i < steps; ++i) {
        scene->Update(stepTime);

        FrameSnapshot snapshot;
        auto& registry = scene->GetRegistry();
        const Entity entityCount = registry.GetEntityCount();
        for (Entity e = 0; e < entityCount; ++e) {
            if (registry.HasComponent<TransformComponent>(e) && registry.HasComponent<RenderComponent>(e)) {
                EntitySnapshot eSnap;
                auto& t = registry.GetComponent<TransformComponent>(e);
                eSnap.position = t.position;
                eSnap.rotation = t.rotation;
                eSnap.scale = t.scale;
                eSnap.matrix = t.matrix;
                eSnap.visible = registry.GetComponent<RenderComponent>(e).visible;
                if (registry.HasComponent<LightComponent>(e)) {
                    eSnap.lightIntensity = registry.GetComponent<LightComponent>(e).intensity;
                } else {
                    eSnap.lightIntensity = 0.0f;
                }
                
                if (registry.HasComponent<PhysicsComponent>(e)) {
                    eSnap.hasPhysics = true;
                    auto& phys = registry.GetComponent<PhysicsComponent>(e);
                    eSnap.velocity = phys.velocity;
                    eSnap.angularVelocity = phys.angularVelocity;
                    eSnap.orientation = phys.orientation;
                    eSnap.isStatic = phys.isStatic;
                    eSnap.forceAccumulator = phys.forceAccumulator;
                    eSnap.torqueAccumulator = phys.torqueAccumulator;
                }
                
                if (registry.HasComponent<ColliderComponent>(e)) {
                    eSnap.hasCollider = true;
                    eSnap.hasCollision = registry.GetComponent<ColliderComponent>(e).hasCollision;
                }

                snapshot.entities[e] = eSnap;
            }
        }
        m_ReplayFrames.push_back(snapshot);
    }

    scene->SetLookaheadMode(false);
    
    // Pass the generated frames count to UI
    editorUI->SetMaxReplayFrames(static_cast<int>(m_ReplayFrames.size()));
    editorUI->SetReplayFrame(0);
    std::cout << "Lookahead generation complete. " << m_ReplayFrames.size() << " frames recorded." << std::endl;
}

void Application::MainLoop() {
    bool hasPendingVSyncApply = false;
    bool pendingVSync = config.vsync;

    float perfLogTimer = 0.0f;
    int perfFrameCount = 0;
    float perfMinDt = std::numeric_limits<float>::max();
    float perfMaxDt = 0.0f;
    double perfUpdateCpuMsAccum = 0.0;
    double perfDrawCpuMsAccum = 0.0;
    int perfHitchCount = 0;

    while (!window->ShouldClose()) {
        if (hasPendingVSyncApply && vulkanSwapChain) {
            config.vsync = pendingVSync;
            vulkanSwapChain->SetVSyncEnabled(config.vsync);
            RecreateSwapChain();
            hasPendingVSyncApply = false;
        }

        const auto frameStart = std::chrono::high_resolution_clock::now();

        const auto currentTime = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        // Clamp simulation delta to avoid huge catch-up spikes when VSync/frame pacing misses a refresh interval.
        const float simDeltaTime = std::min(deltaTime, 1.0f / 30.0f);

        window->PollEvents();
        ProcessInput();

        if (framebufferResized) {
            RecreateSwapChain();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // The Draw call now handles the top bar logic
        std::string nextScene = editorUI->Draw(deltaTime,
            scene->GetWeatherIntensity(),
            scene->GetSeasonName(),
            *scene,
            cameraController->GetOrbitTarget());

        const float* uiColor = editorUI->GetClearColor();
        renderer->SetClearColor(glm::vec4(uiColor[0], uiColor[1], uiColor[2], uiColor[3]));

        // If the user clicked a scene in the "Load Scene" tab, switch now
        if (!nextScene.empty()) {
            LoadScene(nextScene);
        }

        bool requestedVsync = config.vsync;
        int requestedMaxFps = config.maxFps;
        if (editorUI->ConsumePerformanceSettingsRequest(requestedVsync, requestedMaxFps)) {
            config.maxFps = std::max(0, requestedMaxFps);

            if (config.vsync != requestedVsync) {
                pendingVSync = requestedVsync;
                hasPendingVSyncApply = true;
            }
        }

        // Handle physics settings changes
        bool linearDampingEnabled;
        float linearDampingFactor;
        bool quadraticDragEnabled;
        float quadraticDragCoeff;
        if (editorUI->ConsumePhysicsSettingsRequest(linearDampingEnabled, linearDampingFactor, quadraticDragEnabled, quadraticDragCoeff)) {
            PhysicsSystem::SetLinearDamping(linearDampingEnabled, linearDampingFactor);
            PhysicsSystem::SetQuadraticDrag(quadraticDragEnabled, quadraticDragCoeff);
        }

        bool showSpringVisuals = false;
        if (editorUI->ConsumeSpringVisualizationRequest(showSpringVisuals)) {
            scene->SetSpringVisualizationEnabled(showSpringVisuals);
        }

        bool showSpawnerVisuals = false;
        if (editorUI->ConsumeSpawnerVisualizationRequest(showSpawnerVisuals)) {
            scene->SetSpawnerVisualizationEnabled(showSpawnerVisuals);
        }

        ImGui::Render();

        // 1. Handle Restart
        if (editorUI->ConsumeRestartRequest()) {
            ReloadCurrentScene();
        }

        std::string selectedCam = editorUI->ConsumeCameraSwitchRequest();
        if (!selectedCam.empty()) {
            cameraController->SwitchCamera(selectedCam, *scene);
        }

        Entity viewReq = editorUI->ConsumeViewRequest();
        if (viewReq != MAX_ENTITIES) {
            cameraController->SetOrbitTarget(viewReq, *scene);
        }

        auto texRequests = editorUI->ConsumeTextureRequests();
        for (const auto& req : texRequests) {
            renderer->RegisterProceduralTexture(req.name, [req](Texture& tex) {
                if (req.type == EditorUI::ProcTexType::SOLID) {
                    tex.GenerateSolidColor(req.color1);
                }
                else if (req.type == EditorUI::ProcTexType::CHECKERBOARD) {
                    tex.GenerateCheckerboard(512, 512, req.color1, req.color2, req.cellSize);
                }
                else if (req.type == EditorUI::ProcTexType::GRADIENT_VERT) {
                    tex.GenerateGradient(512, 512, req.color1, req.color2, true);
                }
                else if (req.type == EditorUI::ProcTexType::GRADIENT_HORIZ) {
                    tex.GenerateGradient(512, 512, req.color1, req.color2, false);
                }
                });
        }

        // --- PROCESS GEOMETRY CHANGES ---
        auto geoRequests = editorUI->ConsumeGeometryRequests();
        if (!geoRequests.empty()) {
            // CRITICAL: Wait for the GPU to finish the current frame before destroying vertex buffers!
            vkDeviceWaitIdle(vulkanDevice->GetDevice());
            auto& registry = scene->GetRegistry();

            for (const auto& req : geoRequests) {
                if (!registry.HasComponent<RenderComponent>(req.entity)) continue;
                auto& render = registry.GetComponent<RenderComponent>(req.entity);

                // 1. Safely cleanup the old Vulkan memory
                if (render.geometry) {
                    render.geometry->Cleanup();
                }

                // 2. Generate or Load the new geometry
                if (req.type == "Model File" && !req.path.empty()) {
                    std::string ext = req.path.substr(req.path.find_last_of(".") + 1);
                    if (ext == "sjg") {
                        render.geometry = SJGLoader::Load(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), req.path);
                    }
                    else {
                        render.geometry = OBJLoader::Load(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), req.path);
                    }
                }
                else if (req.type == "Cube") {
                    render.geometry = GeometryGenerator::CreateCube(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice());
                }
                else if (req.type == "Sphere") {
                    render.geometry = GeometryGenerator::CreateSphere(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 12, 24, 1.0f);
                }
                else if (req.type == "Bowl") {
                    render.geometry = GeometryGenerator::CreateBowl(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 1.0f, 24, 12);
                }
                else if (req.type == "Terrain") {
                    // Default to a small terrain patch
                    render.geometry = GeometryGenerator::CreateTerrain(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 10.0f, 40, 40, 1.5f, 0.1f);
                }
                else if (req.type == "Plane") {
                    render.geometry = GeometryGenerator::CreatePlane(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice());
                }
                else if (req.type == "Cylinder") {
                    render.geometry = GeometryGenerator::CreateCylinder(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 1.0f, 2.0f, 32);
                }
                else if (req.type == "Disk") {
                    render.geometry = GeometryGenerator::CreateDisk(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 1.0f, 32);
                }
                else if (req.type == "Grid") {
                    render.geometry = GeometryGenerator::CreateGrid(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 20, 20, 0.5f);
                }
            }
        }
        // --------------------------------

        // 2. Calculate advancement
        float stepDelta = 0.0f;
        float currentTimeScale = editorUI->GetTimeScale();

        if (!editorUI->IsPaused()) {
            // Normal running state
            stepDelta = simDeltaTime * currentTimeScale;
        }
        else if (editorUI->ConsumeStepRequest()) {
            // Manual step state - uses the custom step size multiplied by speed
            stepDelta = editorUI->GetStepSize() * currentTimeScale;
        }

        auto& registry = scene->GetRegistry();
        const VkExtent2D extent = vulkanSwapChain->GetExtent();
        const float aspectRatio = (extent.height > 0) ? (extent.width / static_cast<float>(extent.height)) : 1.0f;

        for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
            if (registry.HasComponent<CameraComponent>(e)) {
                registry.GetComponent<CameraComponent>(e).aspectRatio = aspectRatio;
            }
        }


        const auto updateStart = std::chrono::high_resolution_clock::now();
        cameraController->Update(simDeltaTime, *scene, *inputManager);

        // --- Replay Mode Override ---
        if (editorUI->ConsumeGenerateLookaheadRequest(m_LookaheadTimeframe)) {
            GenerateLookahead(m_LookaheadTimeframe);
            editorUI->SetIsReplaying(true);
        }

        if (editorUI->IsReplaying()) {
            m_IsReplaying = true;
            m_CurrentReplayFrame = editorUI->GetReplayFrame();

            if (!m_ReplayFrames.empty() && m_CurrentReplayFrame >= 0 && m_CurrentReplayFrame < m_ReplayFrames.size()) {
                const auto& snapshot = m_ReplayFrames[m_CurrentReplayFrame];
                auto& registry = scene->GetRegistry();
                const Entity entityCount = registry.GetEntityCount();
                for (Entity e = 0; e < entityCount; ++e) {
                    if (registry.HasComponent<TransformComponent>(e) && registry.HasComponent<RenderComponent>(e)) {
                        auto it = snapshot.entities.find(e);
                        if (it != snapshot.entities.end()) {
                            auto& t = registry.GetComponent<TransformComponent>(e);
                            t.position = it->second.position;
                            t.rotation = it->second.rotation;
                            t.scale = it->second.scale;
                            t.matrix = it->second.matrix;

                            if (registry.HasComponent<LightComponent>(e)) {
                                registry.GetComponent<LightComponent>(e).intensity = it->second.lightIntensity;
                            }
                            if (it->second.hasPhysics && registry.HasComponent<PhysicsComponent>(e)) {
                                auto& phys = registry.GetComponent<PhysicsComponent>(e);
                                phys.velocity = it->second.velocity;
                                phys.angularVelocity = it->second.angularVelocity;
                                phys.orientation = it->second.orientation;
                                phys.isStatic = it->second.isStatic;
                                phys.forceAccumulator = it->second.forceAccumulator;
                                phys.torqueAccumulator = it->second.torqueAccumulator;
                            }
                            
                            registry.GetComponent<RenderComponent>(e).visible = it->second.visible;
                            if (it->second.hasCollider && registry.HasComponent<ColliderComponent>(e)) {
                                registry.GetComponent<ColliderComponent>(e).hasCollision = it->second.hasCollision;
                            }
                        } else {
                            // Entity was spawned after this frame, so hide it
                            registry.GetComponent<RenderComponent>(e).visible = false;
                            if (registry.HasComponent<LightComponent>(e)) {
                                registry.GetComponent<LightComponent>(e).intensity = 0.0f;
                            }
                            if (registry.HasComponent<ColliderComponent>(e)) {
                                registry.GetComponent<ColliderComponent>(e).hasCollision = false;
                            }
                            if (registry.HasComponent<PhysicsComponent>(e)) {
                                auto& phys = registry.GetComponent<PhysicsComponent>(e);
                                phys.isStatic = true;
                                phys.velocity = glm::vec3(0.0f);
                                phys.angularVelocity = glm::vec3(0.0f);
                            }
                        }
                    }
                }
            }
        } else {
            m_IsReplaying = false;
            // 4. Update the scene with the calculated delta
            scene->Update(stepDelta);
        }
        
        const auto updateEnd = std::chrono::high_resolution_clock::now();

        Entity activeCamEntity = cameraController->GetActiveCameraEntity();

        int currentViewMask = SceneLayers::ALL;
        int currentInsideRegionMask = 0;

        if (activeCamEntity != MAX_ENTITIES && registry.HasComponent<CameraComponent>(activeCamEntity)) {
            auto& camComp = registry.GetComponent<CameraComponent>(activeCamEntity);

            const glm::mat4 viewMatrix = camComp.viewMatrix;
            const glm::mat4 projMatrix = camComp.projectionMatrix;

            currentViewMask = camComp.viewMask;
            currentInsideRegionMask = camComp.insideRegionMask;

            renderer->DrawFrame(*scene, currentFrame, viewMatrix, projMatrix, currentViewMask, currentInsideRegionMask);
        }
        const auto drawEnd = std::chrono::high_resolution_clock::now();

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        inputManager->Update();

        if (!config.vsync && config.maxFps > 0) {
            const auto targetFrameDuration = std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(config.maxFps))
            );
            const auto frameElapsed = std::chrono::high_resolution_clock::now() - frameStart;
            if (frameElapsed < targetFrameDuration) {
                std::this_thread::sleep_for(targetFrameDuration - frameElapsed);
            }
        }

        if (kPerfDebug) {
            const double updateCpuMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
            const double drawCpuMs = std::chrono::duration<double, std::milli>(drawEnd - updateEnd).count();
            const double frameCpuMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - frameStart).count();

            perfLogTimer += deltaTime;
            perfFrameCount++;
            perfMinDt = std::min(perfMinDt, deltaTime);
            perfMaxDt = std::max(perfMaxDt, deltaTime);
            perfUpdateCpuMsAccum += updateCpuMs;
            perfDrawCpuMsAccum += drawCpuMs;

            if (frameCpuMs > kPerfHitchThresholdMs) {
                perfHitchCount++;
            }

            if (perfLogTimer >= kPerfLogIntervalSeconds && perfFrameCount > 0) {
                const float avgFps = static_cast<float>(perfFrameCount) / perfLogTimer;
                const float minFps = (perfMaxDt > 0.0f) ? (1.0f / perfMaxDt) : 0.0f;
                const float maxFps = (perfMinDt > 0.0f) ? (1.0f / perfMinDt) : 0.0f;
                const double avgUpdateCpuMs = perfUpdateCpuMsAccum / static_cast<double>(perfFrameCount);
                const double avgDrawCpuMs = perfDrawCpuMsAccum / static_cast<double>(perfFrameCount);

                std::cout << std::fixed << std::setprecision(2)
                    << "[Perf] FPS(avg/min/max): " << avgFps << " / " << minFps << " / " << maxFps
                    << " | CPUms(update/draw): " << avgUpdateCpuMs << " / " << avgDrawCpuMs
                    << " | dt(ms): " << (deltaTime * 1000.0f)
                    << " | entities: " << registry.GetEntityCount()
                    << " | renderables: " << scene->GetRenderableEntities().size()
                    << " | vsync: " << (config.vsync ? "ON" : "OFF")
                    << " | hitches(>" << kPerfHitchThresholdMs << "ms): " << perfHitchCount
                    << std::endl;

                perfLogTimer = 0.0f;
                perfFrameCount = 0;
                perfMinDt = std::numeric_limits<float>::max();
                perfMaxDt = 0.0f;
                perfUpdateCpuMsAccum = 0.0;
                perfDrawCpuMsAccum = 0.0;
                perfHitchCount = 0;
            }
        }
    }

    renderer->WaitIdle();
}

void Application::ProcessInput() {
    // --- Application / System ---
    if (inputManager->IsActionJustPressed(InputAction::Exit)) {
        glfwSetWindowShouldClose(window->GetGLFWWindow(), true);
    }

    if (inputManager->IsActionJustPressed(InputAction::PauseToggle)) {
        editorUI->SetPaused(!editorUI->IsPaused());
    }

    auto handleCameraBind = [&](InputAction action, const char* bindName) {
        if (!inputManager->IsActionJustPressed(action)) return;

        const bool wasAlreadyActive = cameraController->IsActiveCameraBoundTo(bindName);
        cameraController->SwitchCameraByBind(bindName, *scene);

        if (wasAlreadyActive) {
            cameraController->CycleRandomTarget(*scene); // no-op unless active camera type is RandomTarget
        }
        };

    // --- Dynamic Camera Switching ---
    // These now look up the assigned camera name from the config file using the ActionBind
    handleCameraBind(InputAction::Camera1, "Camera1");
    handleCameraBind(InputAction::Camera2, "Camera2");
    handleCameraBind(InputAction::Camera3, "Camera3");
    handleCameraBind(InputAction::Camera4, "Camera4");
    handleCameraBind(InputAction::Camera5, "Camera5");
    handleCameraBind(InputAction::Camera6, "Camera6");
    handleCameraBind(InputAction::Camera7, "Camera7");
    handleCameraBind(InputAction::Camera8, "Camera8");

    // --- Decoupled Ignite Logic (F4) ---
    // Works automatically with any camera configured as "RandomTarget" or "Orbit" 
    // that targets a specific object.
    if (inputManager->IsActionJustPressed(InputAction::IgniteTarget)) {
        Entity target = cameraController->GetOrbitTarget();
        if (target != MAX_ENTITIES) {
            scene->Ignite(target);
            std::cout << "Ignited Orbit Target Entity: " << target << std::endl;
        }
        else {
            std::cout << "No valid target in focus to ignite." << std::endl;
        }
    }

    // --- Environment & Rendering Toggles ---
    if (inputManager->IsActionJustPressed(InputAction::ToggleShading)) {
        scene->ToggleGlobalShadingMode();
    }
    if (inputManager->IsActionJustPressed(InputAction::ToggleShadows)) {
        scene->ToggleSimpleShadows();
    }
    if (inputManager->IsActionJustPressed(InputAction::NextSeason)) {
        scene->NextSeason();
    }
    if (inputManager->IsActionJustPressed(InputAction::SpawnDustCloud)) {
        scene->SpawnDustCloud();
    }
    if (inputManager->IsActionJustPressed(InputAction::ToggleWeather)) {
        scene->ToggleWeather();
    }
    if (inputManager->IsActionJustPressed(InputAction::ResetEnvironment)) {
        ReloadCurrentScene();
    }

    if (inputManager->IsActionJustPressed(InputAction::ToggleNoclip)) {
        CameraSystem::ToggleNoclip(*scene);
        std::cout << "Noclip toggled via Hotkey\n";
    }

    const bool sprintHeld = inputManager->IsActionHeld(InputAction::Sprint);

    if (inputManager->IsActionJustPressed(InputAction::FireSpawnerGroupA)) {
        if (sprintHeld) {
            ObjectSpawnerSystem::StartGroup(*scene, "A");
        }
        else {
            ObjectSpawnerSystem::FireGroup(*scene, "A");
        }
    }
    if (inputManager->IsActionJustPressed(InputAction::FireSpawnerGroupB)) {
        if (sprintHeld) {
            ObjectSpawnerSystem::StartGroup(*scene, "B");
        }
        else {
            ObjectSpawnerSystem::FireGroup(*scene, "B");
        }
    }

    // --- Time Speed (Holding T logic) ---
    if (inputManager->IsActionHeld(InputAction::TimeSpeedUp)) {
        const float scaleChangeRate = 2.0f;

        const bool shiftPressed = sprintHeld;
        const bool ctrlPressed = inputManager->IsActionHeld(InputAction::TimeResetModifier);

        if (ctrlPressed) {
            timeScale = 1.0f;
        }
        else if (shiftPressed) {
            timeScale -= scaleChangeRate * deltaTime;
            if (timeScale < 0.1f) timeScale = 0.1f;
        }
        else {
            timeScale += scaleChangeRate * deltaTime;
        }
    }

}

void Application::KeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods) {
    auto* const app = static_cast<Application*>(glfwGetWindowUserPointer(glfwWindow));

    app->inputManager->HandleKeyEvent(key, action);
}

void Application::FramebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height) {
    auto* const app = static_cast<Application*>(glfwGetWindowUserPointer(glfwWindow));
    app->framebufferResized = true;
}

void Application::Cleanup() {
    if (vulkanDevice) {
        vkDeviceWaitIdle(vulkanDevice->GetDevice());
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();


    if (scene) {
        scene->Cleanup();
        scene.reset();
    }

    if (renderer) {
        renderer->Cleanup();
        renderer.reset();
    }

    if (vulkanSwapChain) {
        vulkanSwapChain->Cleanup();
        vulkanSwapChain.reset();
    }

    if (vulkanDevice) {
        vulkanDevice->Cleanup();
        vulkanDevice.reset();
    }

    if (vulkanContext) {
        vulkanContext->Cleanup();
        vulkanContext.reset();
    }

}
