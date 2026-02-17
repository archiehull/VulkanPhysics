#include "Application.h"
#include "../rendering/ParticleLibrary.h"
#include <iostream>
#include <iomanip>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

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

    config = ConfigLoader::Load("config.txt");
    window = std::make_unique<Window>(config.windowWidth, config.windowHeight, "VulkanPhysics");

    // Setup ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    // Note: InitForVulkan needs the window, so do this after window creation
    ImGui_ImplGlfw_InitForVulkan(window->GetGLFWWindow(), true);

    glfwSetWindowUserPointer(window->GetGLFWWindow(), this);
    glfwSetKeyCallback(window->GetGLFWWindow(), KeyCallback);
    glfwSetFramebufferSizeCallback(window->GetGLFWWindow(), FramebufferResizeCallback);
}

void Application::Run() {
    InitVulkan();
    SetupScene();

    lastFrameTime = std::chrono::high_resolution_clock::now();

    const std::string controlsMsg = R"(
--------------------------------------------------
 CONTROLS 
--------------------------------------------------
 [F1]              Outside Camera
 [F2]              Free Roam Camera
 [F3]              Orbit Camera (Random Cactus)
 [F4]              Ignite Orbit Target

 [WASD] / [Arrows] Move Horizontal
 [Q] / [PageDown]  Move Down
 [E] / [PageUp]    Move Up
 [Shift]           Sprint

 [R]               Reset Environment

 [T]               Speed Up Time
 [T] + [Shift]     Slow Down Time
 [T] + [Ctrl]      Normal Time

 [Y]               Toggle Shading (Phong / Gouraud)
 [U]               Toggle Shadows (Simple / Advanced)
 [I]               Next Season
 [O]               Toggle Weather
 [P]               Spawn Dust Cloud


 [Esc]             Exit Application
--------------------------------------------------
)";

    // Print once
    std::cout << controlsMsg << std::endl;

    // Initialize with Static Camera (F1)
    cameraController->SwitchCamera(CameraType::OUTSIDE_ORB, *scene);

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
        window->GetGLFWWindow()
    );
    vulkanSwapChain->Create(vulkanDevice->GetQueueFamilies());
    vulkanSwapChain->CreateImageViews();

    renderer = std::make_unique<Renderer>(
        vulkanDevice.get(),
        vulkanSwapChain.get()
    );
    renderer->Initialize();

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

    cameraController = std::make_unique<CameraController>(config.customCameras);
}

static const char* SUN_NAME = "Sun";
static const char* MOON_NAME = "Moon";
static const char* FOGSHELL_NAME = "FogShell";


void Application::SetupScene() {
    // Pass Configuration to Scene
    scene->SetTimeConfig(config.time);
    scene->SetSeasonConfig(config.seasons);
    scene->SetWeatherConfig(config.weather);
    scene->SetSunHeatBonus(config.sunHeatBonus);

    // Calculate Orbit Speed based on Day Length
    // Speed (rad/s) = 2PI / dayLength
    const float dayLength = config.time.dayLengthSeconds;
    const float baseOrbitSpeed = (dayLength > 0.0f) ? (glm::two_pi<float>() / dayLength) : 0.1f;

    const float sunOrbitSpeed = baseOrbitSpeed;
    const float moonOrbitSpeed = baseOrbitSpeed;

    // Helper to calculate Trajectory (Start Vector direction)
    auto GetTrajectory = [](float directionDegrees) -> glm::vec3 {
        const float rad = glm::radians(directionDegrees);
        glm::vec3 t(cos(rad), 0.0f, sin(rad));
        if (glm::length(t) < 0.001f) t = glm::vec3(1.0f, 0.0f, 0.0f);
        return glm::normalize(t);
        };

    const glm::vec3 sunTrajectory = GetTrajectory(config.sunOrbit.directionDegrees);
    const glm::vec3 moonTrajectory = GetTrajectory(config.moonOrbit.directionDegrees);

    const glm::vec3 sunAxis = glm::normalize(glm::cross(sunTrajectory, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 moonAxis = glm::normalize(glm::cross(moonTrajectory, glm::vec3(0.0f, 1.0f, 0.0f)));

    const glm::vec3 sunStart = sunTrajectory * config.sunOrbit.radius;
    const glm::vec3 moonStart = moonTrajectory * config.moonOrbit.radius;

    const float deltaY = -75.0f;
    const float orbRadius = 150.0f;
    const float adjustedRadius = scene->RadiusAdjustment(orbRadius, deltaY);

    scene->AddTerrain("GroundGrid", adjustedRadius, 512, 512, config.terrainHeightScale, config.terrainNoiseFreq, glm::vec3(0.0f, 0.0f + deltaY, 0.0f), "textures/desert2.jpg");
    scene->AddPedestal("BasePedestal", adjustedRadius, orbRadius * 2.3, 100.0f, glm::vec3(0.0f, 0.0f + deltaY, 0.0f), "textures/mahogany.jpg");
    scene->SetObjectCastsShadow("BasePedestal", false);
    scene->SetObjectLayerMask("BasePedestal", SceneLayers::OUTSIDE);
    scene->SetObjectCollision("BasePedestal", false);

    scene->ClearProceduralRegistry();
    if (config.proceduralPlants.empty()) {
        scene->RegisterProceduralObject("models/cactus.obj", "textures/cactus.jpg", 7.0f, glm::vec3(0.01f), glm::vec3(0.02f), glm::vec3(-90.0f, 0.0f, 0.0f), true);
        scene->RegisterProceduralObject("models/DeadTree.obj", "textures/bark.jpg", 5.0f, glm::vec3(0.1f), glm::vec3(0.2f), glm::vec3(0.0f), true);
        scene->RegisterProceduralObject("models/DeadTree.obj", "textures/bark.jpg", 4.0f, glm::vec3(0.25f), glm::vec3(0.35f), glm::vec3(0.0f), true);
    }
    else {
        for (const auto& plant : config.proceduralPlants) {
            scene->RegisterProceduralObject(plant.modelPath, plant.texturePath, plant.frequency, plant.minScale, plant.maxScale, plant.baseRotation, plant.isFlammable);
        }
    }
    scene->GenerateProceduralObjects(config.proceduralObjectCount, orbRadius - 20, deltaY, config.terrainHeightScale, config.terrainNoiseFreq);
    for (const auto& obj : config.staticObjects) {
        scene->AddModel(obj.name, obj.position, obj.rotation, obj.scale, obj.modelPath, obj.texturePath, obj.isFlammable);
    }

    scene->AddSphere(SUN_NAME, 16, 32, 5.0f, glm::vec3(0.0f), "textures/sun.png");
    scene->AddLight(SUN_NAME, glm::vec3(0.0f), glm::vec3(1.0f, 0.9f, 0.8f), 1.0f, 0);
    scene->SetObjectCastsShadow(SUN_NAME, false);
    scene->SetObjectOrbit(SUN_NAME, glm::vec3(0.0f, 0.0f + deltaY, 0.0f), config.sunOrbit.radius, sunOrbitSpeed, sunAxis, sunStart, config.sunOrbit.initialAngle);
    scene->SetLightOrbit(SUN_NAME, glm::vec3(0.0f, 0.0f + deltaY, 0.0f), config.sunOrbit.radius, sunOrbitSpeed, sunAxis, sunStart, config.sunOrbit.initialAngle);
    scene->SetObjectLayerMask(SUN_NAME, SceneLayers::ALL);
    scene->SetLightLayerMask(SUN_NAME, SceneLayers::ALL);
    scene->SetObjectCollision(SUN_NAME, false);

    scene->AddSphere(MOON_NAME, 16, 32, 2.0f, glm::vec3(0.0f), "textures/moon.jpg");
    scene->AddLight(MOON_NAME, glm::vec3(0.0f), glm::vec3(0.1f, 0.1f, 0.3f), 1.5f, 0);
    scene->SetObjectCastsShadow(MOON_NAME, false);
    scene->SetObjectOrbit(MOON_NAME, glm::vec3(0.0f, 0.0f + deltaY, 0.0f), config.moonOrbit.radius, moonOrbitSpeed, moonAxis, moonStart, config.moonOrbit.initialAngle);
    scene->SetLightOrbit(MOON_NAME, glm::vec3(0.0f, 0.0f + deltaY, 0.0f), config.moonOrbit.radius, moonOrbitSpeed, moonAxis, moonStart, config.moonOrbit.initialAngle);
    scene->SetObjectLayerMask(MOON_NAME, SceneLayers::ALL);
    scene->SetLightLayerMask(MOON_NAME, SceneLayers::ALL);
    scene->SetObjectCollision(MOON_NAME, false);

    scene->AddSphere("PedestalLightSphere", 16, 32, 5.0f, glm::vec3(200.0f, 0.0f, 200.0f));
    scene->AddLight("PedestalLight", glm::vec3(200.0f, 0.0f, 200.0f), glm::vec3(1.0f, 0.5f, 0.2f), 5.0f, 0);
    scene->SetLightLayerMask("PedestalLight", SceneLayers::OUTSIDE);
    scene->SetObjectLayerMask("PedestalLightSphere", SceneLayers::OUTSIDE);
    scene->SetObjectCollision("PedestalLightSphere", false);

    scene->AddSphere("CrystalBall", 32, 64, orbRadius, glm::vec3(0.0f, 0.0f, 0.0f), "");
    scene->SetObjectShadingMode("CrystalBall", 3);
    scene->SetObjectCastsShadow("CrystalBall", false);
    scene->SetObjectCollision("CrystalBall", false);

    scene->AddSphere(FOGSHELL_NAME, 32, 64, orbRadius + 1, glm::vec3(0.0f, 0.0f, 0.0f), "");
    scene->SetObjectShadingMode(FOGSHELL_NAME, 4);
    scene->SetObjectCastsShadow(FOGSHELL_NAME, false);
    scene->SetObjectLayerMask(FOGSHELL_NAME, 0x1 | 0x2);
    scene->SetObjectCollision(FOGSHELL_NAME, false);
}

void Application::RecreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window->GetGLFWWindow(), &width, &height);

    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window->GetGLFWWindow(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(vulkanDevice->GetDevice());

    renderer->Cleanup();
    vulkanSwapChain->Cleanup();

    vulkanSwapChain->Create(vulkanDevice->GetQueueFamilies());
    vulkanSwapChain->CreateImageViews();

    renderer->Initialize();
    renderer->SetupSceneParticles(*scene);

    framebufferResized = false;
}

void Application::MainLoop() {
    while (!window->ShouldClose()) {
        const auto currentTime = std::chrono::high_resolution_clock::now();
        deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();
        lastFrameTime = currentTime;

        window->PollEvents();
        ProcessInput();

        if (framebufferResized) {
            RecreateSwapChain();
        }

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Show a test window
        ImGui::ShowDemoWindow();
        ImGui::Render();

        scene->Update(deltaTime * timeScale);
        cameraController->Update(deltaTime, *scene);

        Camera* const activeCamera = cameraController->GetActiveCamera();

        //// Print camera position every frame
        //if (activeCamera) {
        //    const glm::vec3 pos = activeCamera->GetPosition();
        //    std::cout << std::fixed << std::setprecision(3)
        //        << "Camera Position: (" << pos.x << ", " << pos.y << ", " << pos.z << ")\n";
        //}

        const glm::mat4 viewMatrix = activeCamera->GetViewMatrix();
        const glm::mat4 projMatrix = activeCamera->GetProjectionMatrix(
            vulkanSwapChain->GetExtent().width / static_cast<float>(vulkanSwapChain->GetExtent().height)
        );

        int currentViewMask = SceneLayers::ALL;

        const float dist = glm::length(activeCamera->GetPosition());
        const float ballRadius = 150.0f;

        if (dist < ballRadius) {
            currentViewMask = SceneLayers::INSIDE;
        }
        else {
            currentViewMask = SceneLayers::ALL;
        }

        renderer->DrawFrame(*scene, currentFrame, viewMatrix, projMatrix, currentViewMask);

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    renderer->WaitIdle();
}

void Application::ProcessInput() {
    if (glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window->GetGLFWWindow(), true);
    }

    const bool shiftPressed = glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    const bool ctrlPressed = glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

    if (glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_T) == GLFW_PRESS) {
        const float scaleChangeRate = 2.0f;

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

    if (glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_R) == GLFW_PRESS) {
        timeScale = 1.0f;
        scene->ResetEnvironment();
        std::cout << "Environment Reset." << std::endl;
    }
}

void Application::KeyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods) {
    auto* const app = static_cast<Application*>(glfwGetWindowUserPointer(glfwWindow));

    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_F1) {
            app->cameraController->SwitchCamera(CameraType::OUTSIDE_ORB, *app->scene);
            //std::cout << "Switched to Static Camera (F1)" << std::endl;
        }
        else if (key == GLFW_KEY_F2) {
            app->cameraController->SwitchCamera(CameraType::FREE_ROAM, *app->scene);
            //std::cout << "Switched to Free Roam Camera (F2)" << std::endl;
        }
        else if (key == GLFW_KEY_F3) {
            app->cameraController->SwitchCamera(CameraType::CACTI, *app->scene);
            //std::cout << "Switched to Cactus Orbit Camera (F3)" << std::endl;
        }
        // F4 Ignite Logic
        else if (key == GLFW_KEY_F4) {
            // 1. Ensure we are in Orbit Mode
            if (app->cameraController->GetActiveCameraType() != CameraType::CACTI) {
                app->cameraController->SwitchCamera(CameraType::CACTI, *app->scene);
                //std::cout << "Switched to Cactus Orbit Camera (Auto)" << std::endl;
            }

            // 2. Get and Ignite the target
            SceneObject* const target = app->cameraController->GetOrbitTarget();
            if (target) {
                app->scene->Ignite(target);
            }
            else {
                std::cout << "No target to ignite!" << std::endl;
            }
        }
        else if (key == GLFW_KEY_F5) {
            app->cameraController->SwitchCamera(CameraType::CUSTOM_1, *app->scene);
        }
        else if (key == GLFW_KEY_F6) {
            app->cameraController->SwitchCamera(CameraType::CUSTOM_2, *app->scene);
        }
        else if (key == GLFW_KEY_F7) {
            app->cameraController->SwitchCamera(CameraType::CUSTOM_3, *app->scene);
        }
        else if (key == GLFW_KEY_F8) {
            app->cameraController->SwitchCamera(CameraType::CUSTOM_4, *app->scene);
        }
        else if (key == GLFW_KEY_Y) {
            app->scene->ToggleGlobalShadingMode();
        }
        else if (key == GLFW_KEY_U) {
            app->scene->ToggleSimpleShadows();
        }
        else if (key == GLFW_KEY_I) {
            app->scene->NextSeason();
        }
        else if (key == GLFW_KEY_P) {
            app->scene->SpawnDustCloud();
        }
        else if (key == GLFW_KEY_O) {
            app->scene->ToggleWeather();
        }

        app->cameraController->OnKeyPress(key, true);
    }
    else if (action == GLFW_RELEASE) {
        app->cameraController->OnKeyRelease(key);
    }
}

void Application::FramebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height) {
    auto* const app = static_cast<Application*>(glfwGetWindowUserPointer(glfwWindow));
    app->framebufferResized = true;
}

void Application::Cleanup() {
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