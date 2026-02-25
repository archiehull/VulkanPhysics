#include "EditorUI.h"
#include "imgui.h"
#include "../rendering/ParticleLibrary.h"
#include <algorithm>

void EditorUI::Initialize(const std::string& configPath, const std::string& defaultSceneName) {
    m_ConfigRoot = configPath;
    m_SceneOptions = ConfigLoader::GetAvailableScenes(m_ConfigRoot);

    m_SelectedSceneIndex = 0;
    for (int i = 0; i < (int)m_SceneOptions.size(); i++) {
        if (m_SceneOptions[i].name == defaultSceneName) {
            m_SelectedSceneIndex = i;
            break;
        }
    }
}
void EditorUI::SetInputBindings(const std::unordered_map<std::string, std::string>& bindings) {
    m_DisplayBindings.clear();
    for (const auto& pair : bindings) {
        m_DisplayBindings.push_back({ pair.first, pair.second });
    }
    // Sort alphabetically by action name for a cleaner UI
    std::sort(m_DisplayBindings.begin(), m_DisplayBindings.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
}

std::string EditorUI::GetInitialScenePath() const {
    if (m_SceneOptions.empty()) return "";
    return m_SceneOptions[m_SelectedSceneIndex].path;
}

void EditorUI::SetAvailableCameras(const std::vector<std::string>& cameras) {
    availableCameras = cameras;
}

std::string EditorUI::ConsumeCameraSwitchRequest() {
    std::string req = requestedCamera;
    requestedCamera = "";
    return req; // Clears the request after reading it
}

std::string EditorUI::Draw(float deltaTime, float currentTemp, const std::string& seasonName, Scene& scene, Entity activeOrbitTarget) {
    {
        std::string sceneToLoad = "";

        ImGui::GetIO().FontGlobalScale = m_UIScale;

        if (ImGui::BeginMainMenuBar()) {

            if (ImGui::BeginMenu("#")) {
                ImGui::Text("UI Scale");
                ImGui::SliderFloat("##uiscale", &m_UIScale, 0.5f, 3.0f, "%.2fx");
                ImGui::Separator();
                if (ImGui::Button("Reset UI Scale", ImVec2(-1, 0))) {
                    m_UIScale = 1.0f;
                }

                ImGui::Separator();
                ImGui::MenuItem("View Controls", nullptr, &m_ShowControlsWindow);

                ImGui::EndMenu();
            }

            // --- TAB: Load Scene (Left Aligned) ---
            if (ImGui::BeginMenu("Load Scene")) {
                if (m_SceneOptions.empty()) {
                    ImGui::MenuItem("No scenes found...", nullptr, false, false);
                }
                else {
                    for (int i = 0; i < (int)m_SceneOptions.size(); i++) {
                        const bool isSelected = (m_SelectedSceneIndex == i);
                        if (ImGui::MenuItem(m_SceneOptions[i].name.c_str(), nullptr, isSelected)) {
                            m_SelectedSceneIndex = i;
                            sceneToLoad = m_SceneOptions[i].path;
                        }
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Refresh List", "F5")) {
                    m_SceneOptions = ConfigLoader::GetAvailableScenes(m_ConfigRoot);
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Objects")) {
                Registry& registry = scene.GetRegistry();
                const auto& entities = scene.GetRenderableEntities();

                if (entities.empty()) {
                    ImGui::MenuItem("No objects in scene", nullptr, false, false);
                }
                else {
                    for (Entity e : entities) {
                        // Get name (fallback to Entity ID if no NameComponent exists)
                        std::string entityName = "Entity " + std::to_string(e);
                        if (registry.HasComponent<NameComponent>(e)) {
                            entityName = registry.GetComponent<NameComponent>(e).name;
                        }

                        // --- NEW: Count attached emitters & Check Burning State ---
                        int emitterCount = 0;
                        int fireId = -1;
                        int smokeId = -1;
                        bool isBurning = false;

                        if (registry.HasComponent<ThermoComponent>(e)) {
                            auto& thermo = registry.GetComponent<ThermoComponent>(e);
                            if (thermo.state == ObjectState::BURNING) {
                                isBurning = true;
                            }
                            if (thermo.fireEmitterId != -1) {
                                emitterCount++;
                                fireId = thermo.fireEmitterId;
                            }
                            if (thermo.smokeEmitterId != -1) {
                                emitterCount++;
                                smokeId = thermo.smokeEmitterId;
                            }
                        }

                        // Update the label if it has emitters
                        if (emitterCount > 0) {
                            entityName += " [" + std::to_string(emitterCount) + " Emitters]";
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f)); // Orange/Fire Color
                        }

                        // Apply color if the object is currently burning
                        if (isBurning) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.0f, 1.0f)); // Orange/Fire Color
                        }

                        // Draw the menu item
                        bool menuOpen = ImGui::BeginMenu(entityName.c_str());

                        // IMMEDIATELY pop the color so the contents inside the menu stay normal!
                        if (isBurning) {
                            ImGui::PopStyleColor();
                        }

                        if (emitterCount > 0) {
                            ImGui::PopStyleColor();
						}


                        if (menuOpen) {

                            // Header for visual clarity
                            ImGui::TextDisabled("Entity Properties");

                            // --- NEW: View Object Button ---
                            if (ImGui::Button("View Object", ImVec2(-1, 0))) {
                                m_ViewRequested = e;
                            }

                            // --- NEW: Ignite Object Button ---
                            if (e == activeOrbitTarget && registry.HasComponent<ThermoComponent>(e)) {
                                auto& thermo = registry.GetComponent<ThermoComponent>(e);
                                if (thermo.isFlammable && thermo.state != ObjectState::BURNING) {
                                    // Use the orange/fire color for the ignite button
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.0f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.2f, 0.0f, 1.0f));

                                    if (ImGui::Button("Ignite Object", ImVec2(-1, 0))) {
                                        scene.Ignite(e); // Ignite directly from the UI!
                                    }

                                    ImGui::PopStyleColor(3);
                                }
                            }

                            ImGui::Separator();

                            // 1. Transform / Position
                            if (registry.HasComponent<TransformComponent>(e)) {
                                glm::vec3 pos = registry.GetComponent<TransformComponent>(e).matrix[3];
                                ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
                            }

                            // 2. Render Settings (Layer & Shading)
                            if (registry.HasComponent<RenderComponent>(e)) {
                                auto& render = registry.GetComponent<RenderComponent>(e);
                                const char* layerName = (render.layerMask & SceneLayers::INSIDE) ? "Inside" : "Outside";
                                ImGui::Text("Layer: %s", layerName);

                                const char* modes[] = { "None", "Phong", "Gouraud", "Flat", "Wireframe" };
                                const char* modeName = (render.shadingMode >= 0 && render.shadingMode <= 4) ? modes[render.shadingMode] : "Unknown";
                                ImGui::Text("Shading: %s", modeName);
                            }

                            // 3. Thermodynamics
                            if (registry.HasComponent<ThermoComponent>(e)) {
                                auto& thermo = registry.GetComponent<ThermoComponent>(e);
                                ImGui::Text("Temp: %.1f C", thermo.currentTemp);
                                if (thermo.state == ObjectState::BURNING) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "STATE: BURNING");
                                }
                            }

                            // 4. Material
                            if (registry.HasComponent<RenderComponent>(e)) {
                                auto& render = registry.GetComponent<RenderComponent>(e);
                                ImGui::Separator();
                                ImGui::Text("Texture:");
                                ImGui::TextWrapped("%s", render.texturePath.c_str());
                            }

                            // --- Display Attached Emitter Details ---
                            if (emitterCount > 0) {
                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::TextDisabled("Attached Emitters");

                                const auto& pSystems = scene.GetParticleSystems();

                                auto drawAttachedEmitter = [&](int targetId, const char* label, const std::string& texturePath) {
                                    if (targetId == -1) return;

                                    bool found = false;
                                    for (const auto& sys : pSystems) {
                                        if (sys->GetTexturePath() != texturePath) continue;

                                        for (const auto& em : sys->GetEmitters()) {
                                            if (em.id == targetId) {
                                                std::string menuLabel = std::string(label) + " (ID: " + std::to_string(targetId) + ")";
                                                if (ImGui::BeginMenu(menuLabel.c_str())) {
                                                    ImGui::Text("Rate: %.1f particles/sec", em.particlesPerSecond);
                                                    ImGui::Text("Size: %.2f -> %.2f (Var: %.2f)", em.props.sizeBegin, em.props.sizeEnd, em.props.sizeVariation);
                                                    ImGui::Text("Velocity: (%.1f, %.1f, %.1f)", em.props.velocity.x, em.props.velocity.y, em.props.velocity.z);

                                                    ImGui::Separator();
                                                    if (ImGui::MenuItem("Extinguish Object")) {
                                                        scene.StopObjectFire(e);
                                                    }

                                                    ImGui::EndMenu();
                                                }
                                                found = true;
                                                break;
                                            }
                                        }
                                        if (found) break;
                                    }

                                    if (!found) {
                                        ImGui::MenuItem((std::string(label) + " (ID: " + std::to_string(targetId) + ") - Missing/Stale").c_str(), nullptr, false, false);
                                    }
                                    };

                                drawAttachedEmitter(fireId, "Fire", ParticleLibrary::GetFireProps().texturePath);
                                drawAttachedEmitter(smokeId, "Smoke", ParticleLibrary::GetSmokeProps().texturePath);
                            }

                            ImGui::EndMenu(); // Close the entity's submenu
                        }
                    }
                }
                ImGui::EndMenu(); // Close the "Objects" tab
            }

            // --- TAB: Emitters (Debug) ---
            if (ImGui::BeginMenu("Particles")) {
                const auto& pSystems = scene.GetParticleSystems();

                // 1. Collect all emitters across all systems into a single list
                struct EmitterDebugInfo {
                    int id;
                    std::string texName;
                    ParticleSystem::ParticleEmitter emitter;
                };

                std::vector<EmitterDebugInfo> allEmitters;

                for (const auto& sys : pSystems) {
                    // Extract just the file name from the texture path
                    std::string texName = sys->GetTexturePath();
                    size_t slashPos = texName.find_last_of("/\\");
                    if (slashPos != std::string::npos) {
                        texName = texName.substr(slashPos + 1);
                    }

                    for (const auto& em : sys->GetEmitters()) {
                        allEmitters.push_back({ em.id, texName, em });
                    }
                }

                if (allEmitters.empty()) {
                    ImGui::MenuItem("No Active Emitters", nullptr, false, false);
                }
                else {
                    // 2. Sort them by Emitter ID so they appear in a logical order
                    std::sort(allEmitters.begin(), allEmitters.end(), [](const EmitterDebugInfo& a, const EmitterDebugInfo& b) {
                        return a.id < b.id;
                        });

                    Registry& registry = scene.GetRegistry();
                    const auto& entities = scene.GetRenderableEntities();

                    // 3. Draw the menu items
                    for (const auto& info : allEmitters) {
                        const auto& em = info.emitter;

                        // Menu label shows ID and Type: "Emitter ID: 0 (fire_01.png)"
                        std::string emLabel = "Emitter ID: " + std::to_string(em.id) + " (" + info.texName + ")##" + std::to_string(em.id);

                        if (ImGui::BeginMenu(emLabel.c_str())) {
                            ImGui::TextDisabled("Live Stats");
                            ImGui::Separator();
                            ImGui::Text("Type/Texture: %s", info.texName.c_str());
                            ImGui::Text("Rate: %.1f particles/sec", em.particlesPerSecond);
                            ImGui::Text("Time Since Last Emit: %.4f s", em.timeSinceLastEmit);

                            ImGui::Spacing();
                            ImGui::TextDisabled("Particle Properties");
                            ImGui::Separator();

                            ImGui::Text("Pos: (%.1f, %.1f, %.1f)", em.props.position.x, em.props.position.y, em.props.position.z);
                            ImGui::Text("Pos Var: (%.1f, %.1f, %.1f)", em.props.positionVariation.x, em.props.positionVariation.y, em.props.positionVariation.z);
                            ImGui::Spacing();

                            ImGui::Text("Vel: (%.1f, %.1f, %.1f)", em.props.velocity.x, em.props.velocity.y, em.props.velocity.z);
                            ImGui::Text("Vel Var: (%.1f, %.1f, %.1f)", em.props.velocityVariation.x, em.props.velocityVariation.y, em.props.velocityVariation.z);
                            ImGui::Spacing();

                            ImGui::Text("Size: %.2f -> %.2f (Var: %.2f)", em.props.sizeBegin, em.props.sizeEnd, em.props.sizeVariation);
                            ImGui::Text("Lifetime: %.2f s", em.props.lifeTime);

                            // --- NEW: Attached Objects Section ---
                            ImGui::Spacing();
                            ImGui::TextDisabled("Attached Objects");
                            ImGui::Separator();

                            bool foundAttached = false;
                            for (Entity e : entities) {
                                bool isAttached = false;
                                std::string attachReason = "";

                                // Check ThermoComponent for fire or smoke emitters
                                if (registry.HasComponent<ThermoComponent>(e)) {
                                    auto& thermo = registry.GetComponent<ThermoComponent>(e);
                                    if (thermo.fireEmitterId == em.id) {
                                        isAttached = true;
                                        attachReason = "Fire";
                                    }
                                    if (thermo.smokeEmitterId == em.id) {
                                        isAttached = true;
                                        attachReason += (attachReason.empty() ? "Smoke" : " & Smoke");
                                    }
                                }

                                // Check DustCloudComponent for dust emitters
                                if (registry.HasComponent<DustCloudComponent>(e)) {
                                    auto& dust = registry.GetComponent<DustCloudComponent>(e);
                                    if (dust.emitterId == em.id) {
                                        isAttached = true;
                                        attachReason = "Dust";
                                    }
                                }

                                if (isAttached) {
                                    foundAttached = true;
                                    std::string entityName = "Entity " + std::to_string(e);
                                    if (registry.HasComponent<NameComponent>(e)) {
                                        entityName = registry.GetComponent<NameComponent>(e).name;
                                    }
                                    ImGui::Text(" %s (%s)", entityName.c_str(), attachReason.c_str());
                                }
                            }

                            if (!foundAttached) {
                                ImGui::Text(" None");
                            }

                            ImGui::EndMenu(); // Close Emitter Info
                        }
                    }
                }
                ImGui::EndMenu(); // Close Particle Debug
            }

            // --- TAB: Lights ---
            if (ImGui::BeginMenu("Lights")) {
                Registry& registry = scene.GetRegistry();

                std::vector<Entity> activeLights;
                std::vector<Entity> inactiveLights;

                // 1. Categorize lights into Active and Inactive
                for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
                    if (!registry.HasComponent<LightComponent>(e)) continue;

                    if (registry.GetComponent<LightComponent>(e).intensity > 0.0f) {
                        activeLights.push_back(e);
                    }
                    else {
                        inactiveLights.push_back(e);
                    }
                }

                bool hasLights = !activeLights.empty() || !inactiveLights.empty();

                // 2. Helper lambda so we don't write the UI code twice
                auto drawLightMenu = [&](Entity e) {
                    auto& light = registry.GetComponent<LightComponent>(e);

                    std::string lightName = registry.HasComponent<NameComponent>(e) ?
                        registry.GetComponent<NameComponent>(e).name : "Unnamed Light";

                    std::string menuLabel = lightName + "###LightMenu_" + std::to_string(e);

                    if (ImGui::BeginMenu(menuLabel.c_str())) {

                        if (registry.HasComponent<TransformComponent>(e)) {
                            auto& transform = registry.GetComponent<TransformComponent>(e);
                            glm::vec3 pos = glm::vec3(transform.matrix[3]);

                            ImGui::TextDisabled("Transform Data");
                            ImGui::Separator();
                            ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
                            ImGui::Spacing();
                        }

                        ImGui::TextDisabled("Light Properties");
                        ImGui::Separator();

                        ImGui::ColorEdit3("Color", &light.color.x, ImGuiColorEditFlags_Float);
                        ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f);

                        const char* lightTypes[] = { "Directional", "Point", "Spot" };
                        if (light.type >= 0 && light.type <= 2) {
                            ImGui::Text("Type: %s", lightTypes[light.type]);
                        }
                        else {
                            ImGui::Text("Type ID: %d", light.type);
                        }

                        const char* layerName = (light.layerMask & SceneLayers::INSIDE) ? "Inside" : "Outside";
                        ImGui::Text("Layer: %s", layerName);

                        ImGui::EndMenu();
                    }
                    };

                // 3. Draw Active Lights
                if (!activeLights.empty()) {
                    ImGui::TextDisabled("Active Lights");
                    ImGui::Separator();
                    for (Entity e : activeLights) {
                        drawLightMenu(e);
                    }
                }

                // 4. Draw Inactive Lights in a Sub-Menu
                if (!inactiveLights.empty()) {
                    if (!activeLights.empty()) {
                        ImGui::Spacing();
                    }

                    // Group them in a folder so they don't clutter the main list
                    if (ImGui::BeginMenu("Inactive Lights")) {
                        ImGui::TextDisabled("Pooled / Burned-out Lights");
                        ImGui::Separator();

                        for (Entity e : inactiveLights) {
                            drawLightMenu(e);
                        }
                        ImGui::EndMenu();
                    }
                }

                if (!hasLights) {
                    ImGui::MenuItem("No lights in scene", nullptr, false, false);
                }

                ImGui::EndMenu(); // Close Lights tab
            }

            if (ImGui::BeginMenu("Cameras")) {
                Registry& registry = scene.GetRegistry();
                // Get all entities to find cameras
                for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
                    if (!registry.HasComponent<CameraComponent>(e)) continue;

                    auto& cam = registry.GetComponent<CameraComponent>(e);

                    // 1. Get a safe base name (Fixes crashes if NameComponent is missing)
                    std::string baseCamName = registry.HasComponent<NameComponent>(e) ?
                        registry.GetComponent<NameComponent>(e).name : "Unnamed Camera";

                    // 2. Build the display label separately from the ID
                    std::string menuLabel = baseCamName;

                    // Indicate which camera is currently active and if it's orbiting an object
                    if (cam.isActive) {
                        if (activeOrbitTarget != MAX_ENTITIES) {
                            std::string targetName = "Entity " + std::to_string(activeOrbitTarget);
                            if (registry.HasComponent<NameComponent>(activeOrbitTarget)) {
                                targetName = registry.GetComponent<NameComponent>(activeOrbitTarget).name;
                            }
                            menuLabel += " [VIEWING: " + targetName + "]";
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 1.0f, 1.0f)); // Cyan/Blue
                        }
                        else {
                            menuLabel += " [ACTIVE]";
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f)); // Green
                        }
                    }

                    // 3. Append ### to ensure the ImGui ID NEVER changes, even when the text does!
                    menuLabel += "###CamMenu_" + std::to_string(e);

                    if (ImGui::BeginMenu(menuLabel.c_str())) {
                        if (cam.isActive) ImGui::PopStyleColor();

                        // --- 1. Spatial Data (Transform) ---
                        if (registry.HasComponent<TransformComponent>(e)) {
                            auto& transform = registry.GetComponent<TransformComponent>(e);
                            glm::vec3 pos = glm::vec3(transform.matrix[3]);

                            ImGui::TextDisabled("Spatial Data");
                            ImGui::Separator();
                            ImGui::Text("Position:    (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

                            // Front vector is usually the 3rd column of the rotation matrix (inverse)
                            glm::vec3 front = -glm::normalize(glm::vec3(transform.matrix[2]));
                            ImGui::Text("Front Vector: (%.2f, %.2f, %.2f)", front.x, front.y, front.z);

                            glm::vec3 up = glm::normalize(glm::vec3(transform.matrix[1]));
                            ImGui::Text("Up Vector:    (%.2f, %.2f, %.2f)", up.x, up.y, up.z);
                        }

                        // --- 2. Orientation (Euler Angles) ---
                        ImGui::Spacing();
                        ImGui::TextDisabled("Orientation");
                        ImGui::Separator();
                        ImGui::Text("Yaw:   %.2f", cam.yaw);
                        ImGui::Text("Pitch: %.2f", cam.pitch);

                        // --- 3. Lens & Projection ---
                        ImGui::Spacing();
                        ImGui::TextDisabled("Lens Settings");
                        ImGui::Separator();
                        ImGui::Text("Field of View: %.1f deg", cam.fov);
                        ImGui::Text("Near Plane:    %.2f", cam.nearPlane);
                        ImGui::Text("Far Plane:     %.1f", cam.farPlane);
                        ImGui::Text("Aspect Ratio:  %.2f", cam.aspectRatio);

                        // --- 4. Controls (Speeds) ---
                        ImGui::Spacing();
                        ImGui::TextDisabled("Movement Stats");
                        ImGui::Separator();
                        ImGui::DragFloat("Move Speed", &cam.moveSpeed, 0.5f, 0.1f, 500.0f);
                        ImGui::DragFloat("Rotate Speed", &cam.rotateSpeed, 0.5f, 0.1f, 500.0f);

                        ImGui::Separator();

                        // Use the safe base name instead of querying the component again
                        if (cam.isActive && activeOrbitTarget != MAX_ENTITIES) {
                            if (ImGui::MenuItem("Stop Viewing / Free Camera")) {
                                requestedCamera = baseCamName;
                            }
                        }
                        else {
                            if (ImGui::MenuItem("Switch to this Camera")) {
                                requestedCamera = baseCamName;
                            }
                        }

                        ImGui::EndMenu();
                    }
                    else if (cam.isActive) {
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Simulation")) {
                // 1. Start / Pause Toggle - Using Selectable with DontClosePopups
                std::string pauseLabel = m_IsPaused ? "Start Simulation  [Space]" : "Pause Simulation  [Space]";
                if (ImGui::Selectable(pauseLabel.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                    m_IsPaused = !m_IsPaused;
                }

                ImGui::Separator();

                // 2. Step Configuration
                ImGui::Text("Step Controls");
                ImGui::InputFloat("Step Size (s)", &m_StepSize, 0.001f, 0.01f, "%.4f");

                // Execute Step - Only active when paused, prevents menu autoclose
                if (m_IsPaused) {
                    if (ImGui::Selectable("Execute Step  [F]", false, ImGuiSelectableFlags_DontClosePopups)) {
                        m_StepRequested = true;
                    }
                }
                else {
                    ImGui::TextDisabled("Execute Step  [F] (Pause first)");
                }

                ImGui::Separator();

                // 3. Restart Button
                if (ImGui::MenuItem("Restart Environment", "R")) {
                    m_RestartRequested = true;
                }

                ImGui::Separator();

                // 4. TimeScale Slider - Using Logarithmic scale for massive range (e.g., up to 100x) but fine control below 1x
                ImGui::Text("Simulation Speed (CTRL + CLICK to Type)");
                ImGui::SliderFloat("##speed", &m_TimeScale, 0.0f, 100.0f, "%.3fx", ImGuiSliderFlags_Logarithmic);

                ImGui::EndMenu();
            }

            // --- TAB: Environment ---
            if (ImGui::BeginMenu("Environment")) {

                ImGui::TextDisabled("Live Status");
                ImGui::Separator();
                ImGui::Text("Season: %s", seasonName.c_str());
                ImGui::Text("Global Temp: %.1f C", currentTemp);

                Entity envEntity = scene.GetEnvironmentEntity();
                if (envEntity != MAX_ENTITIES) {
                    auto& env = scene.GetRegistry().GetComponent<EnvironmentComponent>(envEntity);
                    ImGui::Text("Sun Heat Bonus: %.1f", env.sunHeatBonus);
                    ImGui::Text("Weather Intensity: %.2f", env.weatherIntensity);
                    ImGui::Text("Time Since Rain: %.1f s", env.timeSinceLastRain);
                    ImGui::Text("Fire Suppression Timer: %.1f s", env.postRainFireSuppressionTimer);
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Controls");
                ImGui::Separator();

                // --- MOVED: Background Colour Sub-menu ---
                if (ImGui::BeginMenu("Background Colour")) {
                    ImGui::ColorPicker4("##bg_picker", m_ClearColor,
                        ImGuiColorEditFlags_PickerHueWheel |
                        ImGuiColorEditFlags_AlphaBar |
                        ImGuiColorEditFlags_NoSidePreview);

                    ImGui::Separator();
                    if (ImGui::Button("Reset to Default", ImVec2(-1, 0))) {
                        m_ClearColor[0] = 0.1f; m_ClearColor[1] = 0.1f;
                        m_ClearColor[2] = 0.1f; m_ClearColor[3] = 1.0f;
                    }
                    ImGui::EndMenu();
                }

                // 2. Shadows Toggle
                bool useSimple = scene.IsUsingSimpleShadows();
                if (ImGui::Checkbox("Use Simple Shadows", &useSimple)) {
                    scene.ToggleSimpleShadows();
                }

                ImGui::Spacing();

                // 3. Season Control
                if (ImGui::Selectable("Cycle to Next Season", false, ImGuiSelectableFlags_DontClosePopups)) {
                    scene.NextSeason();
                }

                // 4. Weather Control
                bool isPrecipitating = scene.IsPrecipitating();
                std::string weatherLabel = isPrecipitating ? "Stop Weather" : "Start Weather";
                if (ImGui::Selectable(weatherLabel.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                    scene.ToggleWeather();
                }

                // 5. Dust Cloud Control
                bool isDustActive = scene.IsDustActive();
                std::string dustLabel = isDustActive ? "Stop Dust Cloud" : "Spawn Dust Cloud";
                if (ImGui::Selectable(dustLabel.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                    if (isDustActive) {
                        scene.StopDust();
                    }
                    else {
                        scene.SpawnDustCloud();
                    }
                }

                ImGui::EndMenu();
            }

            // --- RIGHT-ALIGNED STATUS AREA ---
            // 1. Prepare strings
            std::string currentSceneName = m_SceneOptions.empty() ? "None" : m_SceneOptions[m_SelectedSceneIndex].name;
            std::string activeSceneStr = "Active Scene: " + currentSceneName;
            std::string fpsStr = std::to_string((int)(1.0f / deltaTime)) + " FPS";

            // 2. Calculate total width for both items plus padding
            float spacing = 20.0f;
            float totalRightWidth = ImGui::CalcTextSize(activeSceneStr.c_str()).x +
                ImGui::CalcTextSize(fpsStr.c_str()).x +
                spacing + 40.0f; // Extra padding from edge

            // 3. Set cursor to push elements to the right
            ImGui::SameLine(ImGui::GetWindowWidth() - totalRightWidth);

            // 4. Draw Active Scene
            ImGui::TextDisabled("Active Scene: ");
            ImGui::SameLine();
            ImGui::Text("%s", currentSceneName.c_str());

            // 5. Draw FPS next to it
            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(fpsStr.c_str()).x - 20.0f);
            ImGui::TextDisabled("%s", fpsStr.c_str());
            ImGui::EndMainMenuBar();
        }

        if (m_ShowControlsWindow) {
            // Set a default size for the window so it doesn't open tiny
            ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

            // Pass the boolean address so ImGui can close it via the 'X' button
            if (ImGui::Begin("Input Controls", &m_ShowControlsWindow)) {

                if (m_DisplayBindings.empty()) {
                    ImGui::TextDisabled("No bindings loaded.");
                }
                else {
                    // Use ImGui Tables for a clean layout
                    if (ImGui::BeginTable("ControlsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {

                        // Table Headers
                        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                        ImGui::TableSetupColumn("Key Bound", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                        ImGui::TableHeadersRow();

                        // Table Rows
                        for (const auto& bind : m_DisplayBindings) {
                            ImGui::TableNextRow();

                            ImGui::TableNextColumn();
                            ImGui::Text("%s", bind.first.c_str());

                            ImGui::TableNextColumn();
                            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%s", bind.second.c_str()); // Greenish text for keys
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::End();
        }

        return sceneToLoad;
    }
}