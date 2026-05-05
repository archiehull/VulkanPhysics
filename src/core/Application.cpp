#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_  // Prevent inclusion of winsock.h by windows.h

#ifdef APIENTRY // hide warning
#undef APIENTRY
#endif

#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include <windows.h> // For CPU affinity
#include <timeapi.h> // For timeBeginPeriod
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <thread>
#include <limits>
#include <cctype>
#include "Application.h"
#include "../rendering/ParticleLibrary.h"
#include <chrono>
#include <vector>
#include <utility>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")

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
#include "FlatBufferSceneLoader.h"

namespace {
    constexpr bool kSceneDebug = false;
    constexpr bool kPerfDebug = false;
    constexpr bool kRuntimeDebug = false;
    constexpr bool kReplayDebug = false;
    constexpr float kRuntimeLogIntervalSeconds = 1.0f;
    constexpr float kPerfLogIntervalSeconds = 1.0f;
    constexpr float kPerfHitchThresholdMs = 20.0f;

    // --- NEW: Adaptive Affinity Masking ---
    DWORD_PTR BuildVisualCoreMask(unsigned int logicalCores, uint16_t portOffset) {
        // Default to Core 0 (Bit 0)
        DWORD_PTR mask = static_cast<DWORD_PTR>(1ull << 0);
        
        // If we have many cores, shift the visual core based on the instance port
        // to prevent Peer 1, Peer 2, etc. from all fighting for Core 0.
        if (logicalCores >= 8) {
            mask = static_cast<DWORD_PTR>(1ull << (portOffset % 4));
        }
        return mask;
    }

    DWORD_PTR BuildSimulationCoreMask(unsigned int logicalCores, uint16_t portOffset) {
        DWORD_PTR mask = 0;
        if (logicalCores >= 4) {
            // Start simulation cores at Core 3 (Bit 2) or higher
            unsigned int startCore = 2;
            
            // If many cores, offset the starting simulation core to separate instances
            if (logicalCores >= 8) {
                startCore = 4 + (portOffset % 2);
            }

            for (unsigned int core = startCore; core < logicalCores; ++core) {
                mask |= (static_cast<DWORD_PTR>(1ull) << core);
            }
        }
        return mask;
    }

    bool ApplyCurrentThreadAffinity(DWORD_PTR mask) {
        if (mask == 0) {
            return false;
        }
        return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
    }

    unsigned int GetLogicalCoreCount() {
        const unsigned int reported = std::thread::hardware_concurrency();
        return (reported == 0) ? 4u : reported;
    }

    float ClampHz(float value, float minHz, float maxHz) {
        return std::clamp(value, minHz, maxHz);
    }

    struct RuntimeControlState {
        bool hasPaused = false;
        bool paused = false;
        bool hasTimeScale = false;
        float timeScale = 1.0f;
        bool hasStepSize = false;
        float stepSize = 0.0166f;
        int stepCount = 0;
    };

    bool NearlyEqual(float a, float b, float eps = 1e-4f) {
        return std::fabs(a - b) <= eps;
    }

    bool ParseBool(const std::string& value) {
        if (value == "1" || value == "true" || value == "True" || value == "TRUE") {
            return true;
        }
        return false;
    }

    RuntimeControlState ParseRuntimeControlPayload(const std::string& payload) {
        RuntimeControlState state;
        std::stringstream ss(payload);
        std::string token;

        while (std::getline(ss, token, ';')) {
            if (token.empty()) continue;
            auto eqPos = token.find('=');
            if (eqPos == std::string::npos) continue;
            std::string key = token.substr(0, eqPos);
            std::string value = token.substr(eqPos + 1);

            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
            value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);

            try {
                if (key == "pause" || key == "paused") {
                    state.hasPaused = true;
                    state.paused = ParseBool(value);
                }
                else if (key == "timescale") {
                    state.hasTimeScale = true;
                    state.timeScale = std::max(0.0f, std::stof(value));
                }
                else if (key == "stepsize") {
                    state.hasStepSize = true;
                    state.stepSize = std::max(0.0f, std::stof(value));
                }
                else if (key == "step" || key == "steps") {
                    state.stepCount = std::max(0, std::stoi(value));
                }
            }
            catch (const std::exception&) {
                continue;
            }
        }

        return state;
    }

    std::string BuildRuntimeControlPayload(bool paused, float timeScale, float stepSize, int stepCount) {
        std::ostringstream out;
        out << "paused=" << (paused ? 1 : 0)
            << ";timeScale=" << timeScale
            << ";stepSize=" << stepSize
            << ";step=" << stepCount;
        return out.str();
    }

    void AddPendingStepTime(std::atomic<float>& pending, float delta) {
        float expected = pending.load();
        while (!pending.compare_exchange_weak(expected, expected + delta)) {
        }
    }
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

    // Enable high-resolution timers on Windows (fix for background "slowness")
    timeBeginPeriod(1);

    m_networkManager = std::make_unique<NetworkManager>();
    m_networkManager->SetDebugLogging(true);

    // Configure peer addresses before starting so the receive thread sees them immediately
    m_networkManager->ConfigurePeer(0, "127.0.0.1", 27015);
    m_networkManager->ConfigurePeer(1, "127.0.0.1", 27016);
    m_networkManager->ConfigurePeer(2, "127.0.0.1", 27017);
    m_networkManager->ConfigurePeer(3, "127.0.0.1", 27018);

    // Register all callbacks before Startup() so no peer-join or reliable event is missed
    m_networkManager->SetReliableEventCallback([this](NetworkEventType type, const std::string& payload, uint32_t target) {
        if (type == NetworkEventType::SceneLoad) {
            std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
            m_TaskQueue.push_back([this, payload]() {
                LoadScene(payload, false);
            });
        }
        else if (type == NetworkEventType::DespawnObject) {
            std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
            m_TaskQueue.push_back([this, target]() {
                if (!scene) return;
                std::unique_lock<std::shared_mutex> ecsLock(m_RegistryMutex);
                scene->DeleteEntity(target);
            });
        }
        else if (type == NetworkEventType::SpawnObject) {
            std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
            m_TaskQueue.push_back([this, payload]() {
                if (!scene) return;
                
                std::unique_lock<std::shared_mutex> ecsLock(m_RegistryMutex);

                try {
                    // Parse CSV payload: geo,px,py,pz,sx,sy,sz,tex,model,mass,vx,vy,vz,avx,avy,avz,owner,id[,spawnTs]
                    std::vector<std::string> tokens;
                    std::stringstream ss(payload);
                    std::string item;
                    while (std::getline(ss, item, ',')) tokens.push_back(item);

                    if (tokens.size() < 18) return;

                    std::string geo = tokens[0];
                    glm::vec3 pos(std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3]));
                    glm::vec3 scale(std::stof(tokens[4]), std::stof(tokens[5]), std::stof(tokens[6]));
                    std::string tex = tokens[7];
                    std::string model = tokens[8];
                    if (model == "none") model = "";
                    float massVal = std::stof(tokens[9]);
                    glm::vec3 vel(std::stof(tokens[10]), std::stof(tokens[11]), std::stof(tokens[12]));
                    glm::vec3 angVel(std::stof(tokens[13]), std::stof(tokens[14]), std::stof(tokens[15]));
                    uint8_t ownerId = static_cast<uint8_t>(std::stoi(tokens[16]));
                    Entity id = static_cast<Entity>(std::stoul(tokens[17]));

                    // Normalise the spawn timestamp from the owning peer's broadcast clock so the seed
                    // is anchored at the actual spawn moment rather than kInterpolationDelay in the past.
                    float normalizedSpawnTs = -1.0f;
                    if (tokens.size() >= 19) {
                        float rawSpawnTs = std::stof(tokens[18]);
                        int senderPeerId = static_cast<int>(ownerId);
                        if (m_networkManager->PeerHasTimestampOffset(senderPeerId)) {
                            normalizedSpawnTs = rawSpawnTs + m_networkManager->GetPeerTimestampOffset(senderPeerId);
                        }
                        // If offset not calibrated yet, fall back to -1 and let SeedRemoteState use the heuristic.
                    }

                    if (scene->GetRegistry().IsAlive(id)) return; // Already exists

                    Entity spawned = MAX_ENTITIES;
                    std::string name = "NetSpawn_" + std::to_string(id);

                    if (geo == "Cube") spawned = scene->AddObjectExplicit(id, name, std::shared_ptr<Geometry>(GeometryGenerator::CreateCube(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice())), pos, tex, false);
                    else if (geo == "Plane") spawned = scene->AddObjectExplicit(id, name, std::shared_ptr<Geometry>(GeometryGenerator::CreatePlane(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), true)), pos, tex, false);
                    else if (geo == "Sphere") spawned = scene->AddObjectExplicit(id, name, std::shared_ptr<Geometry>(GeometryGenerator::CreateSphere(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 16, 32, 0.5f)), pos, tex, false);
                    else if (geo == "Capsule" || geo == "Smoke Grenade") spawned = scene->AddObjectExplicit(id, name, std::shared_ptr<Geometry>(GeometryGenerator::CreateCapsule(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 0.2f, 1.0f, 32, 16)), pos, tex, false);
                    else if (geo == "Model") spawned = scene->AddModel(name, pos, glm::vec3(0), scale, model, tex, false, id); 

                    if (spawned != MAX_ENTITIES) {
                        auto& reg = scene->GetRegistry();
                        auto& tr = reg.GetComponent<TransformComponent>(spawned);
                        tr.scale = scale;
                        tr.UpdateMatrix();

                        if (!reg.HasComponent<PhysicsComponent>(spawned)) reg.AddComponent<PhysicsComponent>(spawned, PhysicsComponent{});
                        auto& p = reg.GetComponent<PhysicsComponent>(spawned);
                        p.isStatic = false;
                        p.SetMass(massVal);
                        p.velocity = vel;
                        p.angularVelocity = angVel;

                        if (!reg.HasComponent<ColliderComponent>(spawned)) reg.AddComponent<ColliderComponent>(spawned, ColliderComponent{});
                        auto& col = reg.GetComponent<ColliderComponent>(spawned);
                        col.hasCollision = true;
                        if (geo == "Sphere") { col.type = 0; col.radius = std::max({ scale.x, scale.y, scale.z }) * 0.5f; }
                        else if (geo == "Capsule" || geo == "Smoke Grenade") { col.type = 2; col.radius = 0.2f * scale.x; col.height = 1.0f * scale.y; }

                        auto ownType = static_cast<ObjectOwnershipType>(ownerId);
                        reg.AddComponent<OwnershipComponent>(spawned, { ownType });

                        // Apply owner-specific visual feedback for remote spawns
                        if (reg.HasComponent<RenderComponent>(spawned)) {
                            auto& render = reg.GetComponent<RenderComponent>(spawned);
                            render.useDebugOverlay = false;
                            render.useColorTint = true;
                            render.tintColor = OwnershipComponent{ ownType }.GetOwnerColor();
                            render.tintColor.a = 1.0f;
                        }

                        // Seed interpolation history anchored at the actual spawn time.
                        // normalizedSpawnTs aligns the seed with the real physics elapsed time,
                        // eliminating the position snap when the first UDP snapshot arrives.
                        m_networkManager->SeedRemoteState(spawned, pos, vel, glm::vec3(0.0f), normalizedSpawnTs);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[Network] Failed to parse SpawnObject event: " << e.what() << std::endl;
                }
            });
        }
        else if (type == NetworkEventType::RuntimeControl) {
            std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
            m_TaskQueue.push_back([this, payload]() {
                ApplyRuntimeControlPayload(payload);
            });
        }
    });

    m_networkManager->SetPeerJoinedCallback([this](int newPeerId) {
        // We are the Host (Peer 0). A new client just joined.
        // Queue this on the main thread so we safely access the ECS registry.
        std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
        m_TaskQueue.push_back([this, newPeerId]() {

            // 1. Tell them to load the exact base world file we are running
            m_networkManager->SendReliableEventTo(newPeerId, NetworkEventType::SceneLoad, currentScenePath);

            // 2. Iterate through ECS and send them all dynamically spawned entities
            if (!scene) return;
            std::shared_lock<std::shared_mutex> readLock(m_RegistryMutex);

            auto& registry = scene->GetRegistry();
            auto transformArray = registry.GetComponentArray<TransformComponent>();
            auto physicsArray = registry.GetComponentArray<PhysicsComponent>();
            auto renderArray = registry.GetComponentArray<RenderComponent>();
            auto ownershipArray = registry.GetComponentArray<OwnershipComponent>();

            // Assuming dynamic objects use partitioned IDs starting at 10,000
            for (Entity e = 10000; e < MAX_ENTITIES; ++e) {
                if (registry.IsAlive(e) && transformArray->HasData(e) && renderArray->HasData(e)) {
                    auto& tr = transformArray->GetData(e);
                    auto& rd = renderArray->GetData(e);

                    float mass = 1.0f;
                    glm::vec3 vel(0.0f);
                    glm::vec3 angVel(0.0f);
                    if (physicsArray->HasData(e)) {
                        auto& p = physicsArray->GetData(e);
                        mass = p.mass;
                        vel = p.velocity;
                        angVel = p.angularVelocity;
                    }

                    uint8_t owner = 0;
                    if (ownershipArray->HasData(e)) {
                        owner = ownershipArray->GetData(e).GetOwnerIndex();
                    }

                    // Recover the spawn type from the geometry name
                    std::string geoStr = "Sphere";
                    std::string modelStr = "";
                    std::string lowerGeo = rd.geometryName;
                    std::transform(lowerGeo.begin(), lowerGeo.end(), lowerGeo.begin(), ::tolower);

                    if (lowerGeo == "cube") geoStr = "Cube";
                    else if (lowerGeo == "plane") geoStr = "Plane";
                    else if (lowerGeo == "capsule") geoStr = "Capsule";
                    else if (lowerGeo.find(".obj") != std::string::npos || lowerGeo.find(".sjg") != std::string::npos) {
                        geoStr = "Model";
                        modelStr = rd.geometryName;
                    }
                    else if (rd.texturePath.find("smoke_grenade") != std::string::npos) {
                        geoStr = "Smoke Grenade";
                    }

                    // Format it EXACTLY like ObjectSpawnerSystem does so the standard TCP receiver parses it
                    std::stringstream ss;
                    ss << geoStr << ","
                        << tr.position.x << "," << tr.position.y << "," << tr.position.z << ","
                        << tr.scale.x << "," << tr.scale.y << "," << tr.scale.z << ","
                        << rd.texturePath << "," << modelStr << ","
                        << mass << ","
                        << vel.x << "," << vel.y << "," << vel.z << ","
                        << angVel.x << "," << angVel.y << "," << angVel.z << ","
                        << (int)owner << "," << e;

                    m_networkManager->SendReliableEventTo(newPeerId, NetworkEventType::SpawnObject, ss.str());
                }
            }
            });
        });

    m_networkManager->SetPeerDisconnectedCallback([this](int disconnectedPeerId) {
        // Queue this on the main thread so we safely access the ECS registry
        std::lock_guard<std::mutex> lock(m_TaskQueueMutex);
        m_TaskQueue.push_back([this, disconnectedPeerId]() {
            if (!scene) return;
            std::unique_lock<std::shared_mutex> ecsLock(m_RegistryMutex);

            // 1. Determine who the new "Authority" is (Lowest surviving Peer ID)
            int localId = m_networkManager->GetLocalPeerId();
            int lowestSurvivingId = localId;
            for (int i = 0; i < 4; ++i) {
                if (i != disconnectedPeerId && m_networkManager->GetPeerStatus(i).connected) {
                    if (i < lowestSurvivingId) {
                        lowestSurvivingId = i; // Someone else with a lower ID is still alive
                    }
                }
            }

            // 2. If WE are the lowest surviving peer, we take ownership of the orphans
            if (localId == lowestSurvivingId) {
                auto& registry = scene->GetRegistry();
                auto ownershipArray = registry.GetComponentArray<OwnershipComponent>();
                const Entity entityCount = registry.GetEntityCount();

                int orphanedCount = 0;
                ObjectOwnershipType newOwnerType = static_cast<ObjectOwnershipType>(localId);

                for (Entity e = 0; e < entityCount; ++e) {
                    if (!registry.IsAlive(e)) continue;

                    // --- 1. Migrate Standard Ownership (The Balls) ---
                    if (ownershipArray->HasData(e)) {
                        auto& ownership = ownershipArray->GetData(e);

                        if (static_cast<int>(ownership.GetOwnerIndex()) == disconnectedPeerId) {

                            // Reassign 
                            ownership.owner = newOwnerType;
                            scene->RegisterLocallyOwnedNetworkEntity(e);
                            m_networkManager->ClearHistoryForEntity(e);
                            orphanedCount++;

                            // Update visual color so you can see it change ownership
                            if (registry.HasComponent<RenderComponent>(e)) {
                                auto& render = registry.GetComponent<RenderComponent>(e);
                                render.useColorTint = true;
                                render.tintColor = ownership.GetOwnerColor();
                            }
                        }
                    }

                    // --- 2. Migrate Spawner Authority ---
                    if (registry.HasComponent<ObjectSpawnerComponent>(e)) {
                        auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);

                        // A) Reassign unowned spawners waiting for the dead peer to fire
                        if (static_cast<int>(spawner.autofireAuthority) == disconnectedPeerId) {
                            spawner.autofireAuthority = static_cast<uint8_t>(lowestSurvivingId);
                        }

                        // B) If the spawner cycles authority or ownership sequentially, it WILL hit 
                        // the dead peer on the next tick and break. Force it to local host mode.
                        spawner.rotateAuthority = false;
                        if (spawner.assignedOwner == ObjectSpawnerComponent::OWNER_SEQUENTIAL) {
                            spawner.assignedOwner = static_cast<uint8_t>(lowestSurvivingId);
                        }

                        // C) Fix spawners hardcoded to assign newly created objects to the dead peer
                        if (spawner.assignedOwner < 4 && static_cast<int>(spawner.assignedOwner) == disconnectedPeerId) {
                            spawner.assignedOwner = static_cast<uint8_t>(lowestSurvivingId);
                        }

                        // D) Preserve dormant spawners. Only keep running if already active
                        // or configured to auto-run.
                        spawner.isRunning = spawner.isRunning || spawner.alwaysOn || spawner.triggerOnStartup;
                    }
                }

                if (orphanedCount > 0) {
                    std::cout << "[Application] Peer " << localId
                        << " assumed control of " << orphanedCount
                        << " orphaned entities from dropped Peer "
                        << disconnectedPeerId << std::endl;
                }
            }
            });
        });

    ObjectSpawnerSystem::onObjectSpawned = [this](const ObjectSpawnerSystem::SpawnEvent& ev) {
        if (m_networkManager && m_networkManager->IsRunning()) {
            // Build CSV: geo,px,py,pz,sx,sy,sz,tex,model,mass,vx,vy,vz,avx,avy,avz,owner,id,spawnTs
            // spawnTs is the raw broadcast clock at spawn time so receivers can anchor the seed correctly.
            std::stringstream ss;
            ss << ev.geometryType << ","
                << ev.position.x << "," << ev.position.y << "," << ev.position.z << ","
                << ev.scale.x << "," << ev.scale.y << "," << ev.scale.z << ","
                << ev.texturePath << "," << ev.modelPath << ","
                << ev.mass << ","
                << ev.velocity.x << "," << ev.velocity.y << "," << ev.velocity.z << ","
                << ev.angularVelocity.x << "," << ev.angularVelocity.y << "," << ev.angularVelocity.z << ","
                << (int)ev.ownerId << "," << ev.entityId << ","
                << m_networkManager->GetCurrentBroadcastTimestamp();

            m_networkManager->SendReliableEvent(NetworkEventType::SpawnObject, ss.str());
        }
    };

    // All callbacks registered — safe to start network threads now
    uint16_t localPort = m_networkManager->Startup();

    const unsigned int logicalCores = GetLogicalCoreCount();
    uint16_t portOffset = (localPort >= 27015) ? (localPort - 27015) : 0;

    m_RenderAffinityMask = static_cast<uint64_t>(BuildVisualCoreMask(logicalCores, portOffset));
    m_SimulationAffinityMask = static_cast<uint64_t>(BuildSimulationCoreMask(logicalCores, portOffset));

    m_RenderThreadId = static_cast<uint32_t>(GetCurrentThreadId());
    m_RenderAffinityApplied = ApplyCurrentThreadAffinity(static_cast<DWORD_PTR>(m_RenderAffinityMask));

    std::string initialPath = editorUI->GetInitialScenePath();
    if (!initialPath.empty()) {
        LoadScene(initialPath);
    }

    lastFrameTime = std::chrono::high_resolution_clock::now();

    m_IsRunning = true;
    m_SimulationThread = std::thread(&Application::SimulationLoop, this);

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

    if (config.customCameras.empty()) {
        CustomCameraConfig defCam;
        defCam.name = "Default";
        defCam.type = "FreeRoam";
        defCam.actionBind = "Camera1";
        defCam.position = glm::vec3(0.0f, 20.0f, 50.0f);
        defCam.pitch = -15.0f;
        config.customCameras.push_back(defCam);
    }

    cameraController = std::make_unique<CameraController>(*scene, config.customCameras);

    std::vector<std::string> camNames;

    // 1. Initialize UI and find the "init" index
    editorUI = std::make_unique<EditorUI>();
    editorUI->Initialize("src/worlds/", "cloth_test");
    editorUI->SetPerformanceSettings(config.vsync, config.maxFps);
    // Use 0.0f to represent "uncapped" (no software FPS cap). Previously code defaulted to 60.
    m_TargetRenderFrequency = static_cast<float>((config.maxFps > 0) ? config.maxFps : 0);
    editorUI->SetRuntimeSettings(m_TargetRenderFrequency.load(), m_TargetSimFrequency.load());

    for (const auto& cam : config.customCameras) {
        camNames.push_back(cam.name);
    }
    editorUI->SetAvailableCameras(camNames);


}

void Application::LoadScene(const std::string& scenePath, bool broadcast) {
    if (broadcast && m_networkManager && m_networkManager->IsRunning()) {
        std::cout << "[Application] Broadcasting SceneLoad: " << scenePath << std::endl;
        m_networkManager->SendReliableEvent(NetworkEventType::SceneLoad, scenePath);
    }
    std::cout << "[Application] LoadScene Initiated: '" << scenePath << "' (Broadcast: " << (broadcast ? "Yes" : "No") << ")" << std::endl;

    // 1. Wait for GPU to finish current frames
    if (vulkanDevice) {
        vkDeviceWaitIdle(vulkanDevice->GetDevice());
    }

    std::unique_lock<std::shared_mutex> lock(m_RegistryMutex);

    // 2. Clear current scene data
    if (scene) {
        scene->Clear();
    }

    if (m_networkManager) {
        m_networkManager->ClearHistory();
    }

    // 3. Load new configuration
    bool isFlatBuffer = scenePath.length() >= 4 && scenePath.substr(scenePath.length() - 4) == ".bin";

    if (isFlatBuffer) {
        config = AppConfig(); // Reset to default config for non-scene things
        currentScenePath = scenePath;
    } else {
        config = ConfigLoader::Load(scenePath);
        currentScenePath = scenePath;
    }
    
    editorUI->SetPerformanceSettings(config.vsync, config.maxFps);

    if (vulkanSwapChain && vulkanSwapChain->IsVSyncEnabled() != config.vsync) {
        vulkanSwapChain->SetVSyncEnabled(config.vsync);
        framebufferResized = true;
    }
    auto activeBindings = inputManager->LoadFromBindings(config.inputBindings);
    editorUI->SetInputBindings(activeBindings);

    // 4. Re-setup scene objects
    try {
        if (isFlatBuffer) {
            renderer->RegisterProceduralTexture("grey_solid", [](Texture& tex) {
                tex.GenerateSolidColor(glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
            });
            FlatBufferSceneLoader::LoadScene(*scene, config, scenePath);
            
            if (scene->GetLights().empty()) {
                scene->AddLight("DefaultSun", glm::vec3(10.0f, 50.0f, 10.0f), glm::vec3(1.0f, 0.98f, 0.95f), 1.2f, 0);
            }
        } else {
            SetupScene();
        }
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

    if (config.customCameras.empty()) {
        CustomCameraConfig defCam;
        defCam.name = "Default";
        defCam.type = "FreeRoam";
        defCam.actionBind = "Camera1";
        defCam.position = glm::vec3(0.0f, 20.0f, 50.0f);
        defCam.pitch = -15.0f;
        config.customCameras.push_back(defCam);
    }

    cameraController = std::make_unique<CameraController>(*scene, config.customCameras);
    std::vector<std::string> camNames;
    for (const auto& cam : config.customCameras) {
        camNames.push_back(cam.name);
    }
    editorUI->SetAvailableCameras(camNames);


    if (renderer && scene) {
        renderer->SetupSceneParticles(*scene);
    }

    if (m_networkManager && m_networkManager->IsRunning()) {
        int localId = m_networkManager->GetLocalPeerId();
        if (localId != -1) {
            scene->GetRegistry().SetNetworkPartition(localId);
        }
    }

    if (kSceneDebug && (m_networkManager)) {
        std::cout << "[Application] LoadScene SUCCESS: " << scenePath << " (Entities: " << scene->GetRegistry().GetEntityCount() << ")" << std::endl;
    }
}

void Application::ReloadCurrentScene() {
    if (currentScenePath.empty()) {
        return;
    }

    LoadScene(currentScenePath);
}

void Application::ApplyRuntimeControlPayload(const std::string& payload) {
    RuntimeControlState state = ParseRuntimeControlPayload(payload);
    if (state.hasPaused) {
        editorUI->SetPaused(state.paused);
    }
    if (state.hasTimeScale) {
        editorUI->SetTimeScale(state.timeScale);
        timeScale.store(state.timeScale);
    }
    if (state.hasStepSize) {
        editorUI->SetStepSize(state.stepSize);
        m_UserStepSize.store(state.stepSize);
    }

    if (state.stepCount > 0) {
        if (!state.hasPaused) {
            editorUI->SetPaused(true);
        }

        const float scale = state.hasTimeScale ? state.timeScale : timeScale.load();
        const float stepSize = state.hasStepSize ? state.stepSize : editorUI->GetStepSize();
        const float stepDelta = stepSize * std::max(0.0f, scale);

        for (int i = 0; i < state.stepCount; ++i) {
            AddPendingStepTime(m_PendingStepTime, stepDelta);
        }
    }

    m_SuppressRuntimeBroadcast = true;
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
    for (auto& objCfg : config.sceneObjects) {
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
            const float r = objCfg.params.x > 0.0f ? objCfg.params.x : 1.0f;
            objCfg.scale *= (r * 2.0f);
            scene->AddSphere(objCfg.name, 12, 24, objCfg.position, objCfg.scale, objCfg.texturePath);
        }
        else if (objCfg.type == "Cylinder") {
            const float r = std::max(0.01f, objCfg.params.x);
            const float h = std::max(0.01f, objCfg.params.y > 0.0f ? objCfg.params.y : 1.0f);
            const int slices = objCfg.params.z > 0.0f ? static_cast<int>(objCfg.params.z) : 32;
            // Unit Cylinder scale based on radius and height
            objCfg.scale *= glm::vec3(r * 2.0f, h, r * 2.0f);
            scene->AddCylinder(objCfg.name, slices, objCfg.position, objCfg.scale, objCfg.texturePath);
        }
        else if (objCfg.type == "Bowl") {
            // Params: x=Radius
            scene->AddBowl(objCfg.name, objCfg.params.x, 24, 12, objCfg.position, objCfg.texturePath);
        }
        else if (objCfg.type == "Cube") {
            scene->AddCube(objCfg.name, objCfg.position, objCfg.scale, objCfg.texturePath);
        }
        else if (objCfg.type == "Plane") {
            scene->AddPlane(objCfg.name, objCfg.position, objCfg.scale, objCfg.texturePath);
        }
        else if (objCfg.type == "Model") {
            // Standard Model
            scene->AddModel(objCfg.name, objCfg.position, objCfg.rotation, objCfg.scale, objCfg.modelPath, objCfg.texturePath, objCfg.isFlammable);
        }
        else if (objCfg.type == "Grid") {
            // Params: x=Rows, y=Cols, z=CellSize
            scene->AddGrid(objCfg.name, (int)objCfg.params.x, (int)objCfg.params.y, objCfg.params.z, objCfg.position, objCfg.texturePath);
        }
        else if (objCfg.type == "Disk") {
            const float r = std::max(0.01f, objCfg.params.x);
            const int slices = objCfg.params.y > 0.0f ? static_cast<int>(objCfg.params.y) : 32;
            // Unit Disk scale based on radius (X/Z = diameter)
            objCfg.scale *= glm::vec3(r * 2.0f, 1.0f, r * 2.0f);
            scene->AddDisk(objCfg.name, slices, objCfg.position, objCfg.scale, objCfg.texturePath);
        }
        else if (objCfg.type == "Capsule") {
            const float r = std::max(0.01f, objCfg.params.x);
            const float h = std::max(0.01f, objCfg.params.y);
            scene->AddCapsule(objCfg.name, r, h, 32, 16, objCfg.position, objCfg.scale, objCfg.texturePath);
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
            spawner.velocityMin = -objCfg.spawnVelocityRandomRange;
            spawner.velocityMax = objCfg.spawnVelocityRandomRange;
            // NEW: angular velocity config
            spawner.spawnAngularVelocity = objCfg.spawnAngularVelocity;
            spawner.randomizeAngularVelocity = objCfg.randomizeSpawnAngularVelocity;
            spawner.angularVelocityMin = -objCfg.spawnAngularVelocityRandomRange;
            spawner.angularVelocityMax = objCfg.spawnAngularVelocityRandomRange;
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
            pathAnim.applyConstantRotation = objCfg.pathApplyConstantRotation;
            pathAnim.rotationSpinRate = objCfg.pathRotationSpinRate;
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

    if (kReplayDebug) {
        std::cout << "[Replay] Generating lookahead for " << timeframe << " seconds..." << std::endl;
    }
    m_ReplayFrames.clear();

    auto& registry = scene->GetRegistry();
    m_LookaheadInitialEntityCount = registry.GetEntityCount();
    Entity highestHijackedId = m_LookaheadInitialEntityCount;
    int currentLocalId = PhysicsSystem::localPeerId;
    if (kReplayDebug) {
        std::cout << "[Replay] Local peer id at lookahead start: " << currentLocalId << std::endl;
    }

    std::vector<std::pair<Entity, ObjectOwnershipType>> originalOwners;
    struct OriginalSpawnerState {
        Entity e;
        uint8_t autofireAuthority;
        bool rotateAuthority;
        bool isRunning;
        float spawnTimer;
        float runElapsedSeconds;
        int spawnedThisRun;
        int spawnedCount;
    };
    std::vector<OriginalSpawnerState> originalSpawners;

    // 1. HIJACK INITIAL REMOTE ENTITIES & SPAWNERS
    for (Entity e = 0; e < highestHijackedId; ++e) {
        if (!registry.IsAlive(e)) continue;

        if (registry.HasComponent<OwnershipComponent>(e)) {
            auto& own = registry.GetComponent<OwnershipComponent>(e);
            if (static_cast<int>(own.owner) != currentLocalId) {
                originalOwners.push_back({ e, own.owner });
                own.owner = static_cast<ObjectOwnershipType>(currentLocalId);
            }
        }

        if (registry.HasComponent<ObjectSpawnerComponent>(e)) {
            auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
            originalSpawners.push_back({
                e,
                spawner.autofireAuthority,
                spawner.rotateAuthority,
                spawner.isRunning,
                spawner.spawnTimer,
                spawner.runElapsedSeconds,
                spawner.spawnedThisRun,
                spawner.spawnedCount
            });
            if (spawner.autofireAuthority != static_cast<uint8_t>(currentLocalId) || spawner.rotateAuthority) {
                spawner.autofireAuthority = static_cast<uint8_t>(currentLocalId);
                spawner.rotateAuthority = false;
            }
        }
    }

    if (kReplayDebug) {
        int spawnerCount = 0;
        int alwaysOnCount = 0;
        for (Entity e = 0; e < highestHijackedId; ++e) {
            if (!registry.IsAlive(e) || !registry.HasComponent<ObjectSpawnerComponent>(e)) continue;
            ++spawnerCount;
            if (registry.GetComponent<ObjectSpawnerComponent>(e).alwaysOn) {
                ++alwaysOnCount;
            }
        }
        std::cout << "[Replay] Spawners at lookahead start: " << spawnerCount
                  << " (alwaysOn=" << alwaysOnCount << ")" << std::endl;
    }

    // 2. CACHE GEOMETRY TO PREVENT MASSIVE VULKAN ALLOCATIONS DURING LOOP
    std::shared_ptr<Geometry> cachedSphere = std::shared_ptr<Geometry>(GeometryGenerator::CreateSphere(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 16, 32, 0.5f));
    std::shared_ptr<Geometry> cachedCube = std::shared_ptr<Geometry>(GeometryGenerator::CreateCube(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice()));
    std::shared_ptr<Geometry> cachedCapsule = std::shared_ptr<Geometry>(GeometryGenerator::CreateCapsule(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 0.2f, 1.0f, 32, 16));

    auto originalSpawnCallback = ObjectSpawnerSystem::onObjectSpawned;
    ObjectSpawnerSystem::onObjectSpawned = [this, cachedSphere, cachedCube, cachedCapsule](const ObjectSpawnerSystem::SpawnEvent& ev) {
        auto& reg = scene->GetRegistry();

        std::shared_ptr<Geometry> geo = cachedSphere;
        if (ev.geometryType == "Cube") geo = cachedCube;
        else if (ev.geometryType == "Capsule" || ev.geometryType == "Smoke Grenade") geo = cachedCapsule;

        if (reg.IsAlive(ev.entityId)) {
            if (reg.HasComponent<RenderComponent>(ev.entityId)) {
                auto& render = reg.GetComponent<RenderComponent>(ev.entityId);
                if (!render.geometry) render.geometry = geo;
            }
        }
        else {
            Entity spawned = scene->AddObjectExplicit(
                ev.entityId,
                "ReplaySpawn_" + std::to_string(ev.entityId),
                geo,
                ev.position,
                ev.texturePath,
                false
            );

            if (spawned != MAX_ENTITIES) {
                auto& tr = reg.GetComponent<TransformComponent>(spawned);
                tr.scale = ev.scale;
                tr.UpdateMatrix();

                if (!reg.HasComponent<PhysicsComponent>(spawned)) reg.AddComponent<PhysicsComponent>(spawned, PhysicsComponent{});
                auto& p = reg.GetComponent<PhysicsComponent>(spawned);
                p.isStatic = false;
                p.SetMass(ev.mass);
                p.velocity = ev.velocity;
                p.angularVelocity = ev.angularVelocity;

                if (!reg.HasComponent<ColliderComponent>(spawned)) reg.AddComponent<ColliderComponent>(spawned, ColliderComponent{});
                auto& col = reg.GetComponent<ColliderComponent>(spawned);
                col.hasCollision = true;
                if (ev.geometryType == "Sphere") { col.type = 0; col.radius = std::max({ ev.scale.x, ev.scale.y, ev.scale.z }) * 0.5f; }
                else if (ev.geometryType == "Capsule" || ev.geometryType == "Smoke Grenade") { col.type = 2; col.radius = 0.2f * ev.scale.x; col.height = 1.0f * ev.scale.y; }

                auto ownType = static_cast<ObjectOwnershipType>(ev.ownerId);
                reg.AddComponent<OwnershipComponent>(spawned, { ownType });

                if (reg.HasComponent<RenderComponent>(spawned)) {
                    auto& render = reg.GetComponent<RenderComponent>(spawned);
                    render.useDebugOverlay = false;
                    render.useColorTint = true;
                    render.tintColor = OwnershipComponent{ ownType }.GetOwnerColor();
                    render.tintColor.a = 1.0f;
                }
            }
        }
        };

    const float stepTime = 1.0f / 60.0f;
    const int steps = static_cast<int>(timeframe / stepTime);

    scene->SetLookaheadMode(true);

    for (int i = 0; i < steps; ++i) {
        // 3. FULL SCENE UPDATE: Ticks Animations, Orbits, Spawners, and Physics
        scene->Update(stepTime);

        const Entity currentEntityCount = registry.GetEntityCount();

        // 4. HIJACK NEWLY SPAWNED ENTITIES
        for (Entity e = highestHijackedId; e < currentEntityCount; ++e) {
            if (!registry.IsAlive(e)) continue;

            if (registry.HasComponent<OwnershipComponent>(e)) {
                auto& own = registry.GetComponent<OwnershipComponent>(e);
                if (static_cast<int>(own.owner) != currentLocalId) {
                    originalOwners.push_back({ e, own.owner });
                    own.owner = static_cast<ObjectOwnershipType>(currentLocalId);
                }
            }

            if (registry.HasComponent<ObjectSpawnerComponent>(e)) {
                auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
                if (spawner.autofireAuthority != currentLocalId || spawner.rotateAuthority) {
                    originalSpawners.push_back({ e, spawner.autofireAuthority, spawner.rotateAuthority });
                    spawner.autofireAuthority = static_cast<uint8_t>(currentLocalId);
                    spawner.rotateAuthority = false;
                }
            }
        }
        highestHijackedId = currentEntityCount;

        // 5. RECORD SNAPSHOT
        FrameSnapshot snapshot;
        for (Entity e = 0; e < currentEntityCount; ++e) {
            if (registry.IsAlive(e) && registry.HasComponent<TransformComponent>(e)) {
                EntitySnapshot eSnap;
                auto& t = registry.GetComponent<TransformComponent>(e);
                eSnap.position = t.position;
                eSnap.rotation = t.rotation;
                eSnap.scale = t.scale;
                eSnap.matrix = t.matrix;
                eSnap.visible = registry.HasComponent<RenderComponent>(e) ? registry.GetComponent<RenderComponent>(e).visible : true;

                if (registry.HasComponent<LightComponent>(e)) {
                    eSnap.lightIntensity = registry.GetComponent<LightComponent>(e).intensity;
                }
                else {
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

                if (registry.HasComponent<PathAnimationComponent>(e)) {
                    eSnap.hasPathAnimation = true;
                    auto& path = registry.GetComponent<PathAnimationComponent>(e);
                    eSnap.pathCurrentTime = path.currentTime;
                    eSnap.pathPlaybackDirection = path.playbackDirection;
                    eSnap.pathIsPlaying = path.isPlaying;
                    eSnap.pathRotationSpinTime = path.rotationSpinTime;
                }

                snapshot.entities[e] = eSnap;
            }
        }
        m_ReplayFrames.push_back(snapshot);
    }

    scene->SetLookaheadMode(false);

    // 6. RESTORE ENGINE STATE
    ObjectSpawnerSystem::onObjectSpawned = originalSpawnCallback;

    for (const auto& pair : originalOwners) {
        if (registry.IsAlive(pair.first) && registry.HasComponent<OwnershipComponent>(pair.first)) {
            registry.GetComponent<OwnershipComponent>(pair.first).owner = pair.second;
        }
    }

    for (const auto& state : originalSpawners) {
        if (state.e >= m_LookaheadInitialEntityCount) continue;
        if (registry.IsAlive(state.e) && registry.HasComponent<ObjectSpawnerComponent>(state.e)) {
            auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(state.e);
            spawner.autofireAuthority = state.autofireAuthority;
            spawner.rotateAuthority = state.rotateAuthority;
            spawner.isRunning = state.isRunning;
            spawner.spawnTimer = state.spawnTimer;
            spawner.runElapsedSeconds = state.runElapsedSeconds;
            spawner.spawnedThisRun = state.spawnedThisRun;
            spawner.spawnedCount = state.spawnedCount;
        }
    }

    editorUI->SetMaxReplayFrames(static_cast<int>(m_ReplayFrames.size()));
    editorUI->SetReplayFrame(0);
    if (kReplayDebug) {
        std::cout << "[Replay] Lookahead generation complete. " << m_ReplayFrames.size() << " frames recorded." << std::endl;
    }
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
    float runtimeLogTimer = 0.0f;
    uint64_t runtimeFrameCount = 0;
    bool wasReplaying = false;

    while (!window->ShouldClose()) {
        if (kRuntimeDebug && runtimeFrameCount == 0) {
            std::cout << "[Runtime] Entered MainLoop." << std::endl;
        }
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
        float simDeltaTime = std::min(deltaTime.load(), 1.0f / 30.0f);
        if (deltaTime > 0.1f) {
            simDeltaTime = 0.0f; // Drop physics frame on huge hitch (e.g. window resize) to prevent explosion
        }

        window->PollEvents();
        ProcessInput();

        editorUI->SetTimeScale(timeScale.load());

        // 1. Process thread-safe tasks from the main thread
        {
            std::vector<std::function<void()>> tasksToRun;
            {
                std::unique_lock<std::mutex> taskLock(m_TaskQueueMutex);
                tasksToRun = std::move(m_TaskQueue);
            }

            // Execute tasks. Note: individual tasks that touch the ECS now take the mutex themselves
            // to prevent recursive deadlock with LoadScene or other locking methods.
            for (auto& task : tasksToRun) {
                task();
            }
        }

        if (framebufferResized) {
            RecreateSwapChain();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // The Draw call now handles the top bar logic
        std::string nextScene;
        {
            std::shared_lock<std::shared_mutex> uiLock(m_RegistryMutex);
            nextScene = editorUI->Draw(deltaTime,
            scene->GetWeatherIntensity(),
            scene->GetSeasonName(),
            *scene,
            cameraController->GetOrbitTarget());
        }

        const float* uiColor = editorUI->GetClearColor();
        renderer->SetClearColor(glm::vec4(uiColor[0], uiColor[1], uiColor[2], uiColor[3]));

        // If the user clicked a scene in the "Load Scene" tab, switch now
        if (!nextScene.empty()) {
            LoadScene(nextScene);
        }

        auto despawnRequests = editorUI->ConsumeDespawnRequests();
        for (auto e : despawnRequests) {
            if (m_networkManager && m_networkManager->IsRunning()) {
                m_networkManager->SendReliableEvent(NetworkEventType::DespawnObject, "", e);
            }
        }

        bool requestedVsync = config.vsync;
        int requestedMaxFps = config.maxFps;
        if (editorUI->ConsumePerformanceSettingsRequest(requestedVsync, requestedMaxFps)) {
            config.maxFps = std::max(0, requestedMaxFps);
            if (config.maxFps > 0) {
                m_TargetRenderFrequency = static_cast<float>(config.maxFps);
            }
            else {
                // 0 represents uncapped rendering
                m_TargetRenderFrequency = 0.0f;
            }
            editorUI->SetRuntimeSettings(m_TargetRenderFrequency.load(), m_TargetSimFrequency.load());

            if (config.vsync != requestedVsync) {
                pendingVSync = requestedVsync;
                hasPendingVSyncApply = true;
            }
        }

        float requestedRenderHz = m_TargetRenderFrequency.load();
        float requestedSimulationHz = m_TargetSimFrequency.load();
        if (editorUI->ConsumeRuntimeSettingsRequest(requestedRenderHz, requestedSimulationHz)) {
            // Allow 0.0f for render target to represent "uncapped" (no software frame pacing)
            m_TargetRenderFrequency = ClampHz(requestedRenderHz, 0.0f, 240.0f);
            m_TargetSimFrequency = ClampHz(requestedSimulationHz, 10.0f, 2000.0f);
            config.maxFps = static_cast<int>(std::round(m_TargetRenderFrequency.load()));
            editorUI->SetRuntimeSettings(m_TargetRenderFrequency.load(), m_TargetSimFrequency.load());
        }

        // Handle physics settings changes
        bool linearDampingEnabled;
        float linearDampingFactor;
        bool quadraticDragEnabled;
        float quadraticDragCoeff;
        bool sleepNormalThresholdEnabled;
        float sleepNormalThreshold;
        bool sleepTangentialThresholdEnabled;
        float sleepTangentialThreshold;
        if (editorUI->ConsumePhysicsSettingsRequest(
            linearDampingEnabled,
            linearDampingFactor,
            quadraticDragEnabled,
            quadraticDragCoeff,
            sleepNormalThresholdEnabled,
            sleepNormalThreshold,
            sleepTangentialThresholdEnabled,
            sleepTangentialThreshold)) {
            PhysicsSystem::SetLinearDamping(linearDampingEnabled, linearDampingFactor);
            PhysicsSystem::SetQuadraticDrag(quadraticDragEnabled, quadraticDragCoeff);
            PhysicsSystem::SetSleepThresholds(
                sleepNormalThresholdEnabled,
                sleepNormalThreshold,
                sleepTangentialThresholdEnabled,
                sleepTangentialThreshold);
        }

        auto netReq = editorUI->ConsumeNetworkSettingsRequest();
        if (netReq.applyRequested && m_networkManager) {
            // 1. Update Simulation Conditions
            m_networkManager->SetSimulationConditions(netReq.latencyMs, netReq.jitterMs, netReq.packetLoss);
            m_networkManager->SetInterpolationDelay(netReq.interpolationDelayMs / 1000.0f);
            m_BroadcastInterval.store(netReq.broadcastIntervalMs / 1000.0f);

            // 2. Update Peer Targets
            for (int i = 0; i < 4; ++i) {
                m_networkManager->ReconfigurePeer(i, netReq.peerIps[i], static_cast<uint16_t>(netReq.peerPorts[i]));
            }

            // 3. Update Local ID (if manually overridden)
            if (netReq.localPeerId != m_networkManager->GetLocalPeerId()) {
                m_networkManager->SetLocalPeerId(netReq.localPeerId);
            }

            if (kRuntimeDebug) {
                std::cout << "[Application] Applied new networking configuration from UI." << std::endl;
            }
        }

        int colliderVisMode = 0;
        if (editorUI->ConsumeColliderVisualizationRequest(colliderVisMode)) {
            renderer->colliderVisMode = static_cast<ColliderVisMode>(colliderVisMode);
        }

        bool showSpringVisuals = false;
        if (editorUI->ConsumeSpringVisualizationRequest(showSpringVisuals)) {
            std::unique_lock<std::shared_mutex> lock(m_RegistryMutex);
            scene->SetSpringVisualizationEnabled(showSpringVisuals);
        }

        bool showSpawnerVisuals = false;
        if (editorUI->ConsumeSpawnerVisualizationRequest(showSpawnerVisuals)) {
            std::unique_lock<std::shared_mutex> lock(m_RegistryMutex);
            scene->SetSpawnerVisualizationEnabled(showSpawnerVisuals);
        }

        ImGui::Render();

        const float uiTimeScale = std::max(0.0f, editorUI->GetTimeScale());
        if (!NearlyEqual(uiTimeScale, timeScale.load())) {
            timeScale.store(uiTimeScale);
        }

        const bool uiPaused = editorUI->IsPaused();
        m_UserPaused.store(uiPaused);
        m_UserStepSize.store(std::max(0.0f, editorUI->GetStepSize()));

        const bool stepRequested = editorUI->ConsumeStepRequest();
        if (stepRequested) {
            const float stepDelta = m_UserStepSize.load() * std::max(0.0f, timeScale.load());
            AddPendingStepTime(m_PendingStepTime, stepDelta);

            if (m_networkManager && m_networkManager->IsRunning()) {
                const std::string payload = BuildRuntimeControlPayload(
                    uiPaused,
                    timeScale.load(),
                    m_UserStepSize.load(),
                    1);
                m_networkManager->SendReliableEvent(NetworkEventType::RuntimeControl, payload);
                m_LastSentPaused = uiPaused;
                m_LastSentTimeScale = timeScale.load();
                m_LastSentStepSize = m_UserStepSize.load();
            }
        }

        if (m_networkManager && m_networkManager->IsRunning()) {
            const bool paused = uiPaused;
            const float scale = timeScale.load();
            const float stepSize = m_UserStepSize.load();

            if (m_SuppressRuntimeBroadcast) {
                m_LastSentPaused = paused;
                m_LastSentTimeScale = scale;
                m_LastSentStepSize = stepSize;
                m_SuppressRuntimeBroadcast = false;
            }
            else if (paused != m_LastSentPaused || !NearlyEqual(scale, m_LastSentTimeScale) || !NearlyEqual(stepSize, m_LastSentStepSize)) {
                const std::string payload = BuildRuntimeControlPayload(paused, scale, stepSize, 0);
                m_networkManager->SendReliableEvent(NetworkEventType::RuntimeControl, payload);
                m_LastSentPaused = paused;
                m_LastSentTimeScale = scale;
                m_LastSentStepSize = stepSize;
            }
        }

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
            std::unique_lock<std::shared_mutex> geoLock(m_RegistryMutex);
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
                else if (req.type == "Capsule") {
                    render.geometry = GeometryGenerator::CreateCapsule(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 0.25f, 1.5f, 32, 16);
                }
                else if (req.type == "Grid") {
                    render.geometry = GeometryGenerator::CreateGrid(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 20, 20, 0.5f);
                }
            }
        }
        // --------------------------------

        auto& registry = scene->GetRegistry();
        const VkExtent2D extent = vulkanSwapChain->GetExtent();
        const float aspectRatio = (extent.height > 0) ? (extent.width / static_cast<float>(extent.height)) : 1.0f;

        for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
            if (registry.HasComponent<CameraComponent>(e)) {
                registry.GetComponent<CameraComponent>(e).aspectRatio = aspectRatio;
            }
        }


        const auto updateStart = std::chrono::high_resolution_clock::now();
        cameraController->SetReplayFreeRoam(editorUI->IsReplaying() && editorUI->GetReplayFreeRoam());
        {
            std::unique_lock<std::shared_mutex> camLock(m_RegistryMutex);
            cameraController->Update(simDeltaTime, *scene, *inputManager);
        }

        // --- Replay Mode Override ---
        if (editorUI->ConsumeGenerateLookaheadRequest(m_LookaheadTimeframe)) {
            int lookaheadLocalId = PhysicsSystem::localPeerId;
            if (m_networkManager) {
                lookaheadLocalId = m_networkManager->GetLocalPeerId();
            }
            if (kReplayDebug) {
                std::cout << "[Replay] Cached local peer id for lookahead: " << lookaheadLocalId << std::endl;
            }

            if (m_networkManager) {
                std::cout << "[Replay] Shutting down network BEFORE lookahead generation...\n";
                m_networkManager->Shutdown();
            }

            const int priorLocalId = PhysicsSystem::localPeerId;
            if (lookaheadLocalId != -1) {
                PhysicsSystem::localPeerId = lookaheadLocalId;
            }

            std::unique_lock<std::shared_mutex> lookaheadLock(m_RegistryMutex);
            GenerateLookahead(m_LookaheadTimeframe);
            PhysicsSystem::localPeerId = priorLocalId;
            editorUI->SetIsReplaying(true);
        }

        if (editorUI->IsReplaying()) {
            if (!wasReplaying) {
                if (m_networkManager && m_networkManager->IsRunning()) {
                    std::cout << "[Replay] Entering Lookahead: Disconnecting network...\n";
                    m_networkManager->Shutdown();
                }
                wasReplaying = true;
            }

            m_IsReplaying = true;
            m_CurrentReplayFrame = editorUI->GetReplayFrame();


            {
            std::unique_lock<std::shared_mutex> replayLock(m_RegistryMutex);
                if (!m_ReplayFrames.empty() && m_CurrentReplayFrame >= 0 && m_CurrentReplayFrame < m_ReplayFrames.size()) {
                    const auto& snapshot = m_ReplayFrames[m_CurrentReplayFrame];
                    auto& registry = scene->GetRegistry();
                    const Entity replayCameraEntity = cameraController->GetActiveCameraEntity();
                    const bool allowReplayFreeRoam = editorUI->GetReplayFreeRoam();
                    const Entity entityCount = registry.GetEntityCount();

                    for (Entity e = 0; e < entityCount; ++e) {
                        if (allowReplayFreeRoam && e == replayCameraEntity) {
                            continue;
                        }
                        if (registry.HasComponent<TransformComponent>(e)) {
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

                                if (registry.HasComponent<RenderComponent>(e)) {
                                    registry.GetComponent<RenderComponent>(e).visible = it->second.visible;
                                }
                                if (it->second.hasCollider && registry.HasComponent<ColliderComponent>(e)) {
                                    registry.GetComponent<ColliderComponent>(e).hasCollision = it->second.hasCollision;
                                }
                                if (it->second.hasPathAnimation && registry.HasComponent<PathAnimationComponent>(e)) {
                                    auto& path = registry.GetComponent<PathAnimationComponent>(e);
                                    path.currentTime = it->second.pathCurrentTime;
                                    path.playbackDirection = it->second.pathPlaybackDirection;
                                    path.isPlaying = it->second.pathIsPlaying;
                                    path.rotationSpinTime = it->second.pathRotationSpinTime;
                                }
                            }
                            else {
                                if (allowReplayFreeRoam && e == replayCameraEntity) {
                                    continue;
                                }
                                if (registry.HasComponent<RenderComponent>(e)) {
                                    registry.GetComponent<RenderComponent>(e).visible = false;
                                }
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
            }
            {
                std::unique_lock<std::shared_mutex> visualLock(m_RegistryMutex);
                float replayVisualDelta = 0.0f;
                if (editorUI->IsReplayPlaying()) {
                    replayVisualDelta = editorUI->GetReplayFrameDuration() * editorUI->GetReplayPlaybackSpeed();
                }
                scene->UpdateVisuals(replayVisualDelta);
            }

        } else {
            if (wasReplaying) {
                std::cout << "[Replay] Exiting Lookahead: Reconnecting to network...\n";

                // Delete entities that were spawned during lookahead simulation
                if (scene && m_LookaheadInitialEntityCount > 0) {
                    auto& reg = scene->GetRegistry();
                    const Entity finalCount = reg.GetEntityCount();
                    for (Entity e = m_LookaheadInitialEntityCount; e < finalCount; ++e) {
                        if (reg.IsAlive(e)) scene->DeleteEntity(e);
                    }
                    m_LookaheadInitialEntityCount = 0;
                }

                if (m_networkManager) {
                    m_networkManager->ClearHistory();
                    // Restart() calls Shutdown() then Startup() using the cached base port
                    m_networkManager->Restart();
                }
                wasReplaying = false;
            }
            m_IsReplaying = false;
            
            // Visual systems (Cloth vertex updates, Animation, etc.) are updated on the main thread 
            // to share the workload and avoid stalling the high-frequency physics thread.
            {
                std::unique_lock<std::shared_mutex> visualLock(m_RegistryMutex);
                scene->UpdateVisuals(simDeltaTime);
            }
        }
        
        const auto updateEnd = std::chrono::high_resolution_clock::now();

        Entity activeCamEntity = cameraController->GetActiveCameraEntity();
        bool hadActiveCamera = false;
        bool drewFrame = false;

        int currentViewMask = SceneLayers::ALL;
        int currentInsideRegionMask = 0;

        {
            std::shared_lock<std::shared_mutex> renderLock(m_RegistryMutex);
            if (activeCamEntity != MAX_ENTITIES && registry.HasComponent<CameraComponent>(activeCamEntity)) {
                hadActiveCamera = true;
                auto& camComp = registry.GetComponent<CameraComponent>(activeCamEntity);

                const glm::mat4 viewMatrix = camComp.viewMatrix;
                const glm::mat4 projMatrix = camComp.projectionMatrix;

                currentViewMask = camComp.viewMask;
                currentInsideRegionMask = camComp.insideRegionMask;

                if (renderer->DrawFrame(*scene, currentFrame, viewMatrix, projMatrix, currentViewMask, currentInsideRegionMask)) {
                    framebufferResized = true;
                }
                drewFrame = true;
            }
        }
        const auto drawEnd = std::chrono::high_resolution_clock::now();

        const float updateMs = static_cast<float>(std::chrono::duration<double, std::milli>(updateEnd - updateStart).count());
        const float renderMs = static_cast<float>(std::chrono::duration<double, std::milli>(drawEnd - updateEnd).count());
        editorUI->SetUpdateTime(updateMs);
        editorUI->SetRenderTime(renderMs);
        editorUI->SetPhysicsTime(m_LastPhysicsStepMs.load());

        const float dt = deltaTime.load();
        if (dt > 0.00001f) {
            const float instantaneousRenderHz = 1.0f / dt;
            const float prevRenderHz = m_RenderActualHz.load();
            m_RenderActualHz = (prevRenderHz <= 0.0f) ? instantaneousRenderHz : (0.9f * prevRenderHz + 0.1f * instantaneousRenderHz);
        }

        const bool affinityCompliant =
            m_RenderAffinityApplied.load() &&
            m_SimulationAffinityApplied.load() &&
            (m_RenderAffinityMask != 0) &&
            (m_SimulationAffinityMask != 0);

        editorUI->SetThreadInfo(2, static_cast<unsigned long>(m_RenderAffinityMask | m_SimulationAffinityMask));
        editorUI->SetRuntimeTelemetry(
            m_RenderActualHz.load(),
            m_SimulationActualHz.load(),
            static_cast<unsigned long>(m_RenderAffinityMask),
            static_cast<unsigned long>(m_SimulationAffinityMask),
            m_RenderThreadId.load(),
            m_SimulationThreadId.load(),
            affinityCompliant,
            m_RenderAffinityApplied.load(),
            m_SimulationAffinityApplied.load());

        if (m_networkManager) {
            NetworkTelemetry network;
            network.isRunning = m_networkManager->IsRunning();
            network.localPeerId = m_networkManager->GetLocalPeerId();
            network.localPort = m_networkManager->GetLocalPort();
            network.simulatedLatencyMs = m_networkManager->GetSimulatedLatency();
            network.simulatedPacketLoss = m_networkManager->GetSimulatedPacketLoss();
            network.broadcastIntervalMs = m_BroadcastInterval.load() * 1000.0f;
            network.connectedPeers = m_networkManager->GetPeerCount();
            network.playbackTime = m_networkManager->GetPlaybackTime();
            network.interpolationDelay = m_networkManager->GetInterpolationDelay();
            network.latestRemoteTimestamp = m_networkManager->GetLatestRemoteTimestamp();
            network.remoteEntityCount = m_networkManager->GetRemoteEntityCount();
            for (int i = 0; i < 4; ++i) {
                auto ps = m_networkManager->GetPeerStatus(i);
                network.peers[i].connected = ps.connected;
                network.peers[i].port = ps.port;
                network.peers[i].ip = ps.ip;
                network.peers[i].pingMs = ps.pingMs;
                network.peers[i].packetLossPct = ps.packetLossPct;
            }
            editorUI->SetNetworkTelemetry(network);
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        inputManager->Update();

        const float targetRenderHz = m_TargetRenderFrequency.load();
        if (!config.vsync && targetRenderHz > 0.0f) {
            const auto targetFrameDuration = std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(targetRenderHz))
            );
            const auto frameElapsed = std::chrono::high_resolution_clock::now() - frameStart;
            if (frameElapsed < targetFrameDuration) {
                std::this_thread::sleep_for(targetFrameDuration - frameElapsed);
            }
        } else if (!config.vsync) {
            // If uncapped, avoid a fixed sleep which suffers from coarse OS timer resolution
            // (causes dramatic FPS reductions). Yield the thread instead so the main loop
            // can run as fast as possible while still allowing other threads to proceed.
            std::this_thread::yield();
        }

        if (kRuntimeDebug) {
            runtimeLogTimer += deltaTime;
            runtimeFrameCount++;
            if (runtimeLogTimer >= kRuntimeLogIntervalSeconds) {
                const double updateCpuMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
                const double drawCpuMs = std::chrono::duration<double, std::milli>(drawEnd - updateEnd).count();
                std::cout << "[Runtime] frame=" << runtimeFrameCount
                          << " dt=" << deltaTime
                          << " update_ms=" << updateCpuMs
                          << " draw_ms=" << drawCpuMs
                          << " entities=" << registry.GetEntityCount()
                          << " renderables=" << scene->GetRenderableEntities().size()
                          << " camera=" << (hadActiveCamera ? "yes" : "no")
                          << " drew=" << (drewFrame ? "yes" : "no")
                          << " paused=" << (editorUI->IsPaused() ? "yes" : "no")
                          << " replay=" << (editorUI->IsReplaying() ? "yes" : "no")
                          << std::endl;
                runtimeLogTimer = 0.0f;
            }
        }

        if (kPerfDebug) {
            const double updateCpuMs = std::chrono::duration<double, std::milli>(updateEnd - updateStart).count();
            const double drawCpuMs = std::chrono::duration<double, std::milli>(drawEnd - updateEnd).count();
            const double frameCpuMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - frameStart).count();

            perfLogTimer += deltaTime.load();
            perfFrameCount++;
            perfMinDt = std::min(perfMinDt, deltaTime.load());
            perfMaxDt = std::max(perfMaxDt, deltaTime.load());
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

    const bool sprintHeld = inputManager->IsActionHeld(InputAction::Sprint);
    bool needsReload = false;

    {
        std::unique_lock<std::shared_mutex> lock(m_RegistryMutex);

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
            needsReload = true;
        }

        if (inputManager->IsActionJustPressed(InputAction::ToggleNoclip)) {
            CameraSystem::ToggleNoclip(*scene);
            std::cout << "Noclip toggled via Hotkey\n";
        }

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
    }

    if (needsReload) {
        ReloadCurrentScene();
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
            timeScale.store(timeScale.load() - scaleChangeRate * deltaTime.load());
            if (timeScale < 0.1f) timeScale = 0.1f;
        }
        else {
            timeScale.store(timeScale.load() + scaleChangeRate * deltaTime.load());
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
    m_IsRunning = false;
    if (m_SimulationThread.joinable()) {
        m_SimulationThread.join();
    }

    if (m_networkManager) {
        m_networkManager->Shutdown();
    }

    // Disable high-resolution timers
    timeEndPeriod(1);

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

void Application::SimulationLoop() {
    m_SimulationThreadId = static_cast<uint32_t>(GetCurrentThreadId());
    m_SimulationAffinityApplied = ApplyCurrentThreadAffinity(static_cast<DWORD_PTR>(m_SimulationAffinityMask));

    auto lastTime = std::chrono::high_resolution_clock::now();
    float accumulator = 0.0f;
    int steppedFrames = 0;
    auto hzWindowStart = std::chrono::high_resolution_clock::now();

    float broadcastAccumulator = 0.0f;
    bool wasPaused = false;

    while (m_IsRunning) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float frameTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        const bool userPaused = m_UserPaused.load();
        float stepBudget = 0.0f;

        if (userPaused) {
            if (!wasPaused) {
                accumulator = 0.0f;
            }
            stepBudget = m_PendingStepTime.exchange(0.0f);
            if (stepBudget > 0.0f) {
                accumulator += stepBudget;
            }
        } else {
            // Apply timescale to the advancement of physics time
            float currentScale = timeScale.load();
            accumulator += frameTime * currentScale;
        }
        wasPaused = userPaused;

        float simulatedDt = userPaused ? stepBudget : (frameTime * timeScale.load());

        // Cap the accumulator to prevent "spiral of death" or huge unstable catch-up steps
        if (accumulator > 0.25f) {
            accumulator = 0.25f;
        }

        const float fixedDt = 1.0f / m_TargetSimFrequency.load();

        if (m_networkManager && m_IsRunning && scene) {
            // --- NEW: Detect ID assignment and configure ECS Partition ---
            static int lastConfiguredPeerId = -1;
            int currentPeerId = m_networkManager->GetLocalPeerId();
            if (currentPeerId != -1 && currentPeerId != lastConfiguredPeerId) {
                std::unique_lock<std::shared_mutex> ecsLock(m_RegistryMutex);
                scene->GetRegistry().SetNetworkPartition(currentPeerId);
                lastConfiguredPeerId = currentPeerId;
                PhysicsSystem::localPeerId = currentPeerId;
                if (kRuntimeDebug) {
                    std::cout << "[Application] NETWORK IDENTITY ESTABLISHED: Peer " << currentPeerId
                        << " (Partition: " << (currentPeerId * 10000) << " - " << ((currentPeerId + 1) * 10000 - 1) << ")" << std::endl;
                }
            }
            // -------------------------------------------------------------

            // Only update networking logic once per simulation loop iteration, not twice.
            // These require unique_lock because they modify the Registry (interpolated state).
            std::unique_lock<std::shared_mutex> ecsLock(m_RegistryMutex);
            m_networkManager->ProcessInboundPackets(scene->GetRegistry());
            // Use frameTime (unscaled) for network clock advancing, as network time is independent of local timeScale
            m_networkManager->AdvanceSimulationTime(simulatedDt);
            m_networkManager->UpdateInterpolation(scene->GetRegistry(), simulatedDt);
        }

        // 2. Consume accumulator in fixed steps
        bool stepped = false;
        int stepsThisFrame = 0;
        const int maxStepsPerFrame = 10; // Increased cap slightly

        PhysicsSystem::simulationPaused = userPaused && stepBudget <= 0.0f;

        while (accumulator >= fixedDt && stepsThisFrame < maxStepsPerFrame) {
            if (!m_IsReplaying && scene && (!userPaused || stepBudget > 0.0f)) {
                const auto physStart = std::chrono::high_resolution_clock::now();
                
                {
                    std::unique_lock<std::shared_mutex> ecsLock(m_RegistryMutex);
                    scene->UpdatePhysics(fixedDt);
                }
                
                const auto physEnd = std::chrono::high_resolution_clock::now();
                m_LastPhysicsStepMs = static_cast<float>(std::chrono::duration<double, std::milli>(physEnd - physStart).count());
                stepped = true;
                steppedFrames++;

                // Network broadcast of authoritative state (throttled)
                if (m_networkManager) {
                    broadcastAccumulator += fixedDt;
                    bool forceStepBroadcast = (userPaused && stepBudget > 0.0f);
                    if (broadcastAccumulator >= m_BroadcastInterval.load() || forceStepBroadcast) {
                        broadcastAccumulator = 0.0f;
                        std::shared_lock<std::shared_mutex> ecsReadLock(m_RegistryMutex);
                        m_networkManager->BroadcastState(scene->GetRegistry(), scene->GetLocallyOwnedNetworkEntities());
                    }
                }
            } else {
                // If paused or replaying, just discard the time
                accumulator = 0.0f;
                break;
            }

            accumulator -= fixedDt;
            stepsThisFrame++;

            // Yield briefly to allow the renderer thread to get a shared lock
            std::this_thread::yield();
        }

        if (userPaused) {
            accumulator = 0.0f;
        }
        else if (accumulator > 0.5f) {
            // If we still have a lot of time left, cap it but don't reset to 0 unless we are really falling behind
            accumulator = 0.0f;
        }

        // --- OPTIMIZED: Adaptive background sleep ---
        // Prevent 100% CPU usage but be more aggressive when close to needing a step.
        // This prevents the 15ms "background sleep" penalty on Windows.
        if (!stepped || accumulator < fixedDt) {
            if (accumulator < fixedDt * 0.25f) {
                // We are very caught up, sleep a bit to be nice to the OS
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                // We are close to needing a step, just yield to allow other threads to run
                std::this_thread::yield();
            }
        }

        const auto hzNow = std::chrono::high_resolution_clock::now();
        const float hzWindowSec = std::chrono::duration<float>(hzNow - hzWindowStart).count();
        if (hzWindowSec >= 0.5f) {
            m_SimulationActualHz = static_cast<float>(steppedFrames) / hzWindowSec;
            steppedFrames = 0;
            hzWindowStart = hzNow;
        }
    }
}

// Networking runtime omitted (disabled per user request)
