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

    // 1. Initialize UI and find the "init" index
    editorUI = std::make_unique<EditorUI>();
    editorUI->Initialize("src/config/", "init");

    // 2. Load the scene that the UI has selected as default
    std::string initialPath = editorUI->GetInitialScenePath();
    if (!initialPath.empty()) {
        LoadScene(initialPath);
    }

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
    //std::cout << controlsMsg << std::endl;

    //cameraController->SwitchCamera(CameraType::CUSTOM_1, *scene);

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

    cameraController = std::make_unique<CameraController>(*scene, config.customCameras);

    editorUI = std::make_unique<EditorUI>();
    editorUI->Initialize("src/config/");
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

    // 4. Re-setup scene objects
    SetupScene();

    // 5. Re-initialize systems that depend on the new config
    cameraController = std::make_unique<CameraController>(*scene, config.customCameras);

    if (renderer && scene) {
        renderer->SetupSceneParticles(*scene);
    }

    std::cout << "Loaded Scene: " << scenePath << std::endl;
}

static const char* SUN_NAME = "Sun";
static const char* MOON_NAME = "Moon";
static const char* FOGSHELL_NAME = "FogShell";

void Application::SetupScene() {
    // 1. Pass Global Configuration to Scene
    scene->SetTimeConfig(config.time);
    scene->SetSeasonConfig(config.seasons);
    scene->SetWeatherConfig(config.weather);
    scene->SetSunHeatBonus(config.sunHeatBonus);

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
        std::cout << "Generated Texture: " << texCfg.name << " (" << texCfg.type << ")" << std::endl;
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
    // Defaults:
    float terrainRadius = 150.0f;
    float terrainY = -75.0f;
    float heightScale = 3.5f;
    float noiseFreq = 0.02f;

    // 3. Process Explicit Scene Objects
    for (const auto& objCfg : config.sceneObjects) {

        // --- Geometry Creation ---
        if (objCfg.type == "Terrain") {
            // Params: x=Radius, y=HeightScale, z=NoiseFreq
            scene->AddTerrain(objCfg.name, objCfg.params.x, 512, 512, objCfg.params.y, objCfg.params.z, objCfg.position, objCfg.texturePath);

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
            scene->AddSphere(objCfg.name, 16, 32, objCfg.params.x, objCfg.position, objCfg.texturePath);
        }
        else if (objCfg.type == "Bowl") {
            // Params: x=Radius
            scene->AddBowl(objCfg.name, objCfg.params.x, 32, 16, objCfg.position, objCfg.texturePath);
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

        // --- Apply Common Properties ---
        // (We assume the object was just added to the back of the vector)
        scene->SetObjectVisible(objCfg.name, objCfg.visible);
        scene->SetObjectCastsShadow(objCfg.name, objCfg.castsShadow);
        scene->SetObjectReceivesShadows(objCfg.name, objCfg.receiveShadows);
        scene->SetObjectShadingMode(objCfg.name, objCfg.shadingMode);
        scene->SetObjectLayerMask(objCfg.name, objCfg.layerMask);
        scene->SetObjectCollision(objCfg.name, objCfg.hasCollision);

        // --- Apply Light ---
        if (objCfg.isLight) {
            scene->AddLight(objCfg.name, objCfg.position, objCfg.lightColor, objCfg.lightIntensity, objCfg.lightType);
            scene->SetLightLayerMask(objCfg.name, objCfg.layerMask);
        }

        // --- Apply Orbit ---
        if (objCfg.hasOrbit) {
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
                const float dayLength = config.time.dayLengthSeconds;
                speed = (dayLength > 0.0f) ? (glm::two_pi<float>() / dayLength) : 0.1f;
            }

            // Apply to Object
            scene->SetObjectOrbit(objCfg.name, objCfg.position, objCfg.orbitRadius, speed, axis, startVector, objCfg.orbitInitialAngle);

            // Apply to Light (if it exists)
            if (objCfg.isLight) {
                scene->SetLightOrbit(objCfg.name, objCfg.position, objCfg.orbitRadius, speed, axis, startVector, objCfg.orbitInitialAngle);
            }
        }
    }

    // 4. Generate Procedural Vegetation
    // We use the terrainRadius we captured (minus buffer) to ensure plants spawn on the terrain
    if (!config.proceduralPlants.empty()) {
        scene->GenerateProceduralObjects(config.proceduralObjectCount, terrainRadius - 20.0f, terrainY, heightScale, noiseFreq);
    }

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

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // The Draw call now handles the top bar logic
        std::string nextScene = editorUI->Draw(deltaTime,
            scene->GetWeatherIntensity(),
            scene->GetSeasonName(),
            *scene);

        const float* uiColor = editorUI->GetClearColor();
        renderer->SetClearColor(glm::vec4(uiColor[0], uiColor[1], uiColor[2], uiColor[3]));

        // If the user clicked a scene in the "Load Scene" tab, switch now
        if (!nextScene.empty()) {
            LoadScene(nextScene);
        }

        ImGui::Render();

        // 1. Handle Restart
        if (editorUI->ConsumeRestartRequest()) {
            scene->ResetEnvironment();
        }

        // 2. Calculate advancement
        float stepDelta = 0.0f;
        float currentTimeScale = editorUI->GetTimeScale();

        if (!editorUI->IsPaused()) {
            // Normal running state
            stepDelta = deltaTime * currentTimeScale;
        }
        else if (editorUI->ConsumeStepRequest()) {
            // Manual step state - uses the custom step size multiplied by speed
            stepDelta = editorUI->GetStepSize() * currentTimeScale;
        }

        // 4. Update the scene with the calculated delta
        scene->Update(stepDelta);

        cameraController->Update(deltaTime, *scene);


        int currentViewMask = SceneLayers::ALL;

        //const float dist = glm::length(activeCamera->GetPosition());
        //const float ballRadius = 150.0f;

        //if (dist < ballRadius) {
        //    currentViewMask = SceneLayers::INSIDE;
        //}
        //else {
        //    currentViewMask = SceneLayers::ALL;
        //}

        Entity activeCamEntity = cameraController->GetActiveCameraEntity(); //
        auto& registry = scene->GetRegistry(); //

        if (activeCamEntity != MAX_ENTITIES && registry.HasComponent<CameraComponent>(activeCamEntity)) {
            auto& camComp = registry.GetComponent<CameraComponent>(activeCamEntity); //

            const glm::mat4 viewMatrix = camComp.viewMatrix; //
            const glm::mat4 projMatrix = camComp.projectionMatrix; //

            // Use these for your renderer call
            renderer->DrawFrame(*scene, currentFrame, viewMatrix, projMatrix, currentViewMask);
        }

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

    //if (glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_R) == GLFW_PRESS) {
    //    timeScale = 1.0f;
    //    scene->ResetEnvironment();
    //    std::cout << "Environment Reset." << std::endl;
    //}
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
            // Switch to Cacti mode if not already there
            if (app->cameraController->GetActiveCameraType() != CameraType::CACTI) {
                app->cameraController->SwitchCamera(CameraType::CACTI, *app->scene);
            }

            // Use the new GetActiveCameraEntity() to find what we are looking at
            Entity camEnt = app->cameraController->GetActiveCameraEntity(); //
            auto& reg = app->scene->GetRegistry(); //

            if (camEnt != MAX_ENTITIES && reg.HasComponent<OrbitComponent>(camEnt)) {
                // Since the camera is orbiting an entity, we ignite the target of that orbit
                // You might need to add a 'targetEntity' field to OrbitComponent if not there,
                // or keep tracking it in the Controller.
                Entity target = app->cameraController->GetOrbitTarget();
                if (target != MAX_ENTITIES) {
                    app->scene->Ignite(target);
                }
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
        if (key == GLFW_KEY_SPACE) {
            app->editorUI->SetPaused(!app->editorUI->IsPaused());
        }
        else if (key == GLFW_KEY_F && app->editorUI->IsPaused()) {
            // Note: You may need a way to trigger StepRequest here 
            // or simply call scene->Update(0.0166f) once.
        }
        // Update existing R key to also sync with UI if needed
        else if (key == GLFW_KEY_R) {
            app->scene->ResetEnvironment();
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