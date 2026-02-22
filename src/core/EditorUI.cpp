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

std::string EditorUI::GetInitialScenePath() const {
    if (m_SceneOptions.empty()) return "";
    return m_SceneOptions[m_SelectedSceneIndex].path;
}

std::string EditorUI::Draw(float deltaTime, float currentTemp, const std::string& seasonName, Scene& scene) {
    {
        std::string sceneToLoad = "";

        if (ImGui::BeginMainMenuBar()) {

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

            if (ImGui::BeginMenu("Background Colour")) {
                // We use a hidden label "##" so the picker doesn't show its own text label
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
            if (ImGui::BeginMenu("Particle Emitters")) {
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

            if (ImGui::BeginMenu("Simulation")) {
                // 1. Start / Pause Toggle
                std::string pauseLabel = m_IsPaused ? "Start Simulation" : "Pause Simulation";
                if (ImGui::MenuItem(pauseLabel.c_str(), "Space")) {
                    m_IsPaused = !m_IsPaused;
                }

                ImGui::Separator();

                // 2. Step Configuration
                ImGui::Text("Step Controls");

                // InputFloat allows precise text entry for the step duration
                ImGui::InputFloat("Step Size (s)", &m_StepSize, 0.001f, 0.01f, "%.4f");

                if (ImGui::MenuItem("Execute Step", "F", false, m_IsPaused)) {
                    m_StepRequested = true;
                }

                ImGui::Separator();

                // 3. Restart Button
                if (ImGui::MenuItem("Restart Environment", "R")) {
                    m_RestartRequested = true;
                }

                ImGui::Separator();

                // 4. TimeScale Slider
                ImGui::Text("Simulation Speed");
                ImGui::SliderFloat("##speed", &m_TimeScale, 0.0f, 5.0f, "%.2fx");

                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Properties")) {
                
                bool useSimple = scene.IsUsingSimpleShadows();

                // Notice: NO '&' symbol before !useSimple
                if (ImGui::MenuItem("Normal", nullptr, !useSimple)) {
                    if (useSimple) {
                        scene.ToggleSimpleShadows();
                    }
                }

                // Notice: NO '&' symbol before useSimple
                if (ImGui::MenuItem("Simple", nullptr, useSimple)) {
                    if (!useSimple) {
                        scene.ToggleSimpleShadows();
                    }
                }

                ImGui::EndMenu();
			}

            // --- TAB: Environment (Left Aligned) ---
            //if (ImGui::BeginMenu("Environment")) {
            //    ImGui::MenuItem((std::string("Season: ") + seasonName).c_str(), nullptr, false, false);
            //    ImGui::MenuItem((std::string("Temp: ") + std::to_string((int)currentTemp) + " C").c_str(), nullptr, false, false);
            //    ImGui::EndMenu();
            //}

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

        return sceneToLoad;
    }
}