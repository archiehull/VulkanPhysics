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
    glm::vec3 position;
    glm::vec3 rotation;
    glm::vec3 scale;
    glm::mat4 matrix;
    bool visible;
    float lightIntensity;

    // Physics state
    bool hasPhysics = false;
    glm::vec3 velocity;
    glm::vec3 angularVelocity;
    glm::mat3 orientation;
    bool isStatic;
    glm::vec3 forceAccumulator;
    glm::vec3 torqueAccumulator;

    // Collider state
    bool hasCollider = false;
    bool hasCollision;
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

    void GenerateLookahead(float timeframe);

    void LoadScene(const std::string& scenePath);

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

    std::chrono::time_point<std::chrono::high_resolution_clock> lastFrameTime;

    AppConfig config;
    std::string currentScenePath;

    std::atomic<float> deltaTime = 0.0f;
    std::atomic<float> timeScale = 1.0f;

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

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
};