#pragma once

#include "../core/Window.h"
#include "../vulkan/VulkanContext.h"
#include "../vulkan/VulkanDevice.h"
#include "../vulkan/VulkanSwapChain.h"
#include "../rendering/Renderer.h"
#include "../rendering/Scene.h"
#include "../rendering/CameraController.h"
#include "../menu/EditorUI.h"
#include "InputManager.h"
#include "../network/NetworkManager.h"

#include "Config.h"

#include <memory>
#include <chrono>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <mutex>
#include <functional>
#include <vector>
#include <cstdint>

struct EntitySnapshot {
    // Transform / render state
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    glm::mat4 matrix = glm::mat4(1.0f);
    bool visible = true;
    float lightIntensity = 1.0f;

    // Physics state
    bool hasPhysics = false;
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 angularVelocity = glm::vec3(0.0f);
    glm::mat3 orientation = glm::mat3(1.0f);
    bool isStatic = false;
    glm::vec3 forceAccumulator = glm::vec3(0.0f);
    glm::vec3 torqueAccumulator = glm::vec3(0.0f);

    // Collider state
    bool hasCollider = false;
    bool hasCollision = false;

    // Animation state
    bool hasPathAnimation = false;
    float pathCurrentTime = 0.0f;
    int pathPlaybackDirection = 1;
    bool pathIsPlaying = false;
    float pathRotationSpinTime = 0.0f;
};

struct FrameSnapshot {
    std::unordered_map<Entity, EntitySnapshot> entities;
};

class Application final {
public:
    Application();
    ~Application() = default;

    void Run();

private:
    void InitVulkan();
    void SetupScene();
    void MainLoop();
    void Cleanup();
    void RecreateSwapChain();
    void ReloadCurrentScene();

    void SimulationLoop();
    // Networking is currently disabled (omitted per request)

    void ApplyRuntimeControlPayload(const std::string& payload);

    void GenerateLookahead(float timeframe);

    void LoadScene(const std::string& scenePath, bool broadcast = true);

    std::vector<SceneOption> sceneOptions;

    int selectedSceneIndex = 0;

    // Input handling
    void ProcessInput();
    static void KeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods);
    static void FramebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height);

    std::unique_ptr<InputManager> inputManager;
    std::unique_ptr<Window> window;
    std::unique_ptr<VulkanContext> vulkanContext;
    std::unique_ptr<VulkanDevice> vulkanDevice;
    std::unique_ptr<VulkanSwapChain> vulkanSwapChain;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<CameraController> cameraController;
    std::unique_ptr<EditorUI> editorUI;
    std::unique_ptr<NetworkManager> m_networkManager;

    std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameTime;

    AppConfig config;
    std::string currentScenePath;

    std::atomic<float> deltaTime = 0.0f;
    std::atomic<float> timeScale = 1.0f;
	std::atomic<float> m_BroadcastInterval = 0.033f; // Default 30Hz broadcast
    std::atomic<bool> m_UserPaused = false;
    std::atomic<float> m_PendingStepTime = 0.0f;
    std::atomic<float> m_UserStepSize = 0.0166f;

    uint32_t currentFrame = 0;
    bool framebufferResized = false;

    // Multithreading and Affinity
    std::thread m_SimulationThread;
    std::atomic<bool> m_IsRunning = false;
    std::atomic<float> m_TargetRenderFrequency = 60.0f;
    std::atomic<float> m_TargetSimFrequency = 120.0f;
    std::shared_mutex m_RegistryMutex;
    std::mutex m_TaskQueueMutex;
    std::vector<std::function<void()>> m_TaskQueue;

    uint64_t m_RenderAffinityMask = 0;
    uint64_t m_SimulationAffinityMask = 0;

    std::atomic<float> m_RenderActualHz = 0.0f;
    std::atomic<float> m_SimulationActualHz = 0.0f;
    std::atomic<float> m_LastPhysicsStepMs = 0.0f;

    std::atomic<uint32_t> m_RenderThreadId = 0;
    std::atomic<uint32_t> m_SimulationThreadId = 0;

    std::atomic<bool> m_RenderAffinityApplied = false;
    std::atomic<bool> m_SimulationAffinityApplied = false;

    // Lookahead Replay State
    std::vector<FrameSnapshot> m_ReplayFrames;
    bool m_IsReplaying = false;
    int m_CurrentReplayFrame = 0;
    float m_LookaheadTimeframe = 5.0f;
    Entity m_LookaheadInitialEntityCount = 0;

    bool m_SuppressRuntimeBroadcast = false;
    bool m_LastSentPaused = false;
    float m_LastSentTimeScale = 1.0f;
    float m_LastSentStepSize = 0.0166f;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
};