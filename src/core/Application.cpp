#include "Application.h"
#include "../rendering/ParticleLibrary.h"
#include <iostream>
#include <iomanip>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "../geometry/GeometryGenerator.h"
#include "../geometry/OBJLoader.h"
#include "../geometry/SJGLoader.h"

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
    editorUI->Initialize("src/worlds/", "desert");

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
    auto activeBindings = inputManager->LoadFromBindings(config.inputBindings);
    editorUI->SetInputBindings(activeBindings);

    // 4. Re-setup scene objects
    SetupScene();

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

    std::cout << "Loaded Scene: " << scenePath << std::endl;
}


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
            *scene,
            cameraController->GetOrbitTarget());

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
                    render.geometry = GeometryGenerator::CreateSphere(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 16, 32, 1.0f);
                }
                else if (req.type == "Bowl") {
                    render.geometry = GeometryGenerator::CreateBowl(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 1.0f, 32, 16);
                }
                else if (req.type == "Terrain") {
                    // Default to a small terrain patch
                    render.geometry = GeometryGenerator::CreateTerrain(vulkanDevice->GetDevice(), vulkanDevice->GetPhysicalDevice(), 10.0f, 64, 64, 1.5f, 0.1f);
                }
            }
        }
        // --------------------------------

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

        auto& registry = scene->GetRegistry();
        const VkExtent2D extent = vulkanSwapChain->GetExtent();
        const float aspectRatio = (extent.height > 0) ? (extent.width / static_cast<float>(extent.height)) : 1.0f;

        for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
            if (registry.HasComponent<CameraComponent>(e)) {
                registry.GetComponent<CameraComponent>(e).aspectRatio = aspectRatio;
            }
        }

        // 4. Update the scene with the calculated delta
        scene->Update(stepDelta);

        cameraController->Update(deltaTime, *scene, *inputManager);

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

        if (activeCamEntity != MAX_ENTITIES && registry.HasComponent<CameraComponent>(activeCamEntity)) {
            auto& camComp = registry.GetComponent<CameraComponent>(activeCamEntity); //

            const glm::mat4 viewMatrix = camComp.viewMatrix; //
            const glm::mat4 projMatrix = camComp.projectionMatrix; //

            // Use these for your renderer call
            renderer->DrawFrame(*scene, currentFrame, viewMatrix, projMatrix, currentViewMask);
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

        inputManager->Update();
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

    // --- Dynamic Camera Switching ---
    // These now look up the assigned camera name from the config file using the ActionBind
    if (inputManager->IsActionJustPressed(InputAction::Camera1)) cameraController->SwitchCameraByBind("Camera1", *scene);
    if (inputManager->IsActionJustPressed(InputAction::Camera2)) cameraController->SwitchCameraByBind("Camera2", *scene);
    if (inputManager->IsActionJustPressed(InputAction::Camera3)) cameraController->SwitchCameraByBind("Camera3", *scene);
    if (inputManager->IsActionJustPressed(InputAction::Camera4)) cameraController->SwitchCameraByBind("Camera4", *scene);
    if (inputManager->IsActionJustPressed(InputAction::Camera5)) cameraController->SwitchCameraByBind("Camera5", *scene);
    if (inputManager->IsActionJustPressed(InputAction::Camera6)) cameraController->SwitchCameraByBind("Camera6", *scene);
    if (inputManager->IsActionJustPressed(InputAction::Camera7)) cameraController->SwitchCameraByBind("Camera7", *scene);
    if (inputManager->IsActionJustPressed(InputAction::Camera8)) cameraController->SwitchCameraByBind("Camera8", *scene);

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
        scene->ResetEnvironment();
    }

    // --- Time Speed (Holding T logic) ---
    if (inputManager->IsActionHeld(InputAction::TimeSpeedUp)) {
        const float scaleChangeRate = 2.0f;

        // Grab modifiers directly from GLFW since they act as modifiers here
        const bool shiftPressed = glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        const bool ctrlPressed = glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
            glfwGetKey(window->GetGLFWWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

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

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(glfwWindow, true);
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