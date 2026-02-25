#pragma once

#include "../core/Window.h"
#include "../vulkan/VulkanContext.h"
#include "../vulkan/VulkanDevice.h"
#include "../vulkan/VulkanSwapChain.h"
#include "../rendering/Renderer.h"
#include "../rendering/Scene.h"
#include "../rendering/CameraController.h"
#include "../core/EditorUI.h"
#include "InputManager.h"

#include "Config.h"

#include <memory>
#include <chrono>

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

    float deltaTime = 0.0f;
    float timeScale = 1.0f;

    uint32_t currentFrame = 0;
    bool framebufferResized = false;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
};