#include "EditorUI.h"
#include "imgui.h"
#include "../rendering/ParticleLibrary.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

void EditorUI::Initialize(const std::string& configPath, const std::string& defaultSceneName) {
    m_ConfigRoot = configPath;

    namespace fs = std::filesystem;
    if (!fs::exists(m_ConfigRoot) || !fs::is_directory(m_ConfigRoot)) {
        std::cerr << "Error: EditorUI config path not found or not a directory: " << m_ConfigRoot << std::endl;
        std::exit(EXIT_FAILURE);
    }

    m_SceneOptions = ConfigLoader::GetAvailableScenes(m_ConfigRoot);

    m_SelectedSceneIndex = 0;
    for (int i = 0; i < (int)m_SceneOptions.size(); i++) {
        if (m_SceneOptions[i].name == defaultSceneName) {
            m_SelectedSceneIndex = i;
            break;
        }
    }

    RefreshTextureList();
    RefreshModelList();
}

void EditorUI::RefreshModelList() {
    m_AvailableModels.clear();
    namespace fs = std::filesystem;
    std::string path = "models";

    if (fs::exists(path) && fs::is_directory(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                // Support both OBJ and your custom SJG format
                if (ext == ".obj" || ext == ".sjg") {
                    std::string p = entry.path().string();
                    std::replace(p.begin(), p.end(), '\\', '/');
                    m_AvailableModels.push_back(p);
                }
            }
        }
    }
}

void EditorUI::RefreshTextureList() {
    m_AvailableTextures.clear();
    namespace fs = std::filesystem;
    std::string path = "textures";

    if (fs::exists(path) && fs::is_directory(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                // Convert extension to lowercase for safe comparison
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") {
                    std::string p = entry.path().string();
                    std::replace(p.begin(), p.end(), '\\', '/'); // Standardize slashes
                    m_AvailableTextures.push_back(p);
                }
            }
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
                ImGui::MenuItem("Entity Properties", nullptr, &m_ShowEntityPropertiesWindow);

                // --- NEW: Create Entity Button ---
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f)); // Make it green
                if (ImGui::MenuItem("Create New Entity")) {
                    static int newEntityCount = 1;
                    std::string name = "NewEntity_" + std::to_string(newEntityCount++);

                    // Spawn a default 1x1 cube. AddCube safely registers it in the 
                    // Scene's Renderable arrays and attaches all base components!
                    scene.AddCube(name, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "");

                    // Force open the properties window so the user can edit it
                    m_ShowEntityPropertiesWindow = true;
                }
                ImGui::PopStyleColor();

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

                        // Count attached emitters & Check Burning State
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

                        // Check custom attached emitters
                        if (registry.HasComponent<AttachedEmitterComponent>(e)) {
                            emitterCount += static_cast<int>(registry.GetComponent<AttachedEmitterComponent>(e).emitters.size());
                        }

                        // Check if this is the currently viewed object
                        bool isViewing = (e == activeOrbitTarget && activeOrbitTarget != MAX_ENTITIES);

                        // Update the label strings
                        if (isViewing) {
                            entityName += " [VIEWING]";
                        }
                        if (emitterCount > 0) {
                            entityName += " [" + std::to_string(emitterCount) + " Emitters]";
                        }

                        // Unified Color Priority System
                        int popCount = 0;
                        if (isViewing) {
                            // Cyan/Blue for viewing (Highest priority)
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
                            popCount++;
                        }
                        else if (isBurning) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
                            popCount++;
                        }
                        else if (emitterCount > 0) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                            popCount++;
                        }

                        // Create a stable ID so the menu doesn't collapse
                        std::string menuLabel = entityName + "###ObjMenu_" + std::to_string(e);

                        bool menuOpen = ImGui::BeginMenu(menuLabel.c_str());

                        if (popCount > 0) {
                            ImGui::PopStyleColor(popCount);
                        }

                        if (menuOpen) {

                            ImGui::TextDisabled("Entity Properties");

                            if (ImGui::Button("View Object", ImVec2(-1, 0))) {
                                m_ViewRequested = e;
                            }

                            if (e == activeOrbitTarget && registry.HasComponent<ThermoComponent>(e)) {
                                auto& thermo = registry.GetComponent<ThermoComponent>(e);
                                if (thermo.isFlammable && thermo.state != ObjectState::BURNING) {
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.0f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
                                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.2f, 0.0f, 1.0f));

                                    if (ImGui::Button("Ignite Object", ImVec2(-1, 0))) {
                                        scene.Ignite(e);
                                    }

                                    ImGui::PopStyleColor(3);
                                }
                            }

                            // Attach Light Button
                            if (registry.HasComponent<NameComponent>(e)) {
                                if (!registry.HasComponent<LightComponent>(e)) {
                                    if (ImGui::Button("Attach Light", ImVec2(-1, 0))) {
                                        std::string targetName = registry.GetComponent<NameComponent>(e).name;
                                        glm::vec3 pos = glm::vec3(0.0f);
                                        if (registry.HasComponent<TransformComponent>(e)) {
                                            pos = glm::vec3(registry.GetComponent<TransformComponent>(e).matrix[3]);
                                        }
                                        scene.AddLight(targetName, pos, glm::vec3(1.0f, 1.0f, 1.0f), 200.0f, 2);
                                    }
                                }
                                else {
                                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "  [Light Attached - Edit in Lights Menu]");
                                }
                            }

                            ImGui::Separator();

                            if (!registry.HasComponent<AttachedEmitterComponent>(e)) {
                                registry.AddComponent<AttachedEmitterComponent>(e, AttachedEmitterComponent{});
                            }
                            auto& attached = registry.GetComponent<AttachedEmitterComponent>(e);

                            if (!attached.emitters.empty()) {
                                ImGui::TextDisabled("Active Emitters");
                                for (size_t i = 0; i < attached.emitters.size(); ++i) {
                                    auto& em = attached.emitters[i];

                                    ImGui::PushID(i);
                                    std::string label = "Remove Emitter ID: " + std::to_string(em.emitterId);
                                    if (em.duration > 0.0f) {
                                        label += " (" + std::to_string((int)(em.duration - em.timer)) + "s left)";
                                    }
                                    else {
                                        label += " (Infinite)";
                                    }

                                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                                    if (ImGui::MenuItem(label.c_str())) {
                                        scene.GetOrCreateSystem(em.props)->StopEmitter(em.emitterId);
                                        attached.emitters.erase(attached.emitters.begin() + i);
                                        ImGui::PopStyleColor();
                                        ImGui::PopID();
                                        break;
                                    }
                                    ImGui::PopStyleColor();
                                    ImGui::PopID();
                                }
                                ImGui::Separator();
                            }

                            if (ImGui::BeginMenu("Attach New Emitter...")) {
                                static float emitDuration = -1.0f;
                                ImGui::InputFloat("Duration (s)", &emitDuration);
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set to -1 for Infinite");
                                ImGui::Separator();

                                auto attachFunc = [&](ParticleProps props, float rate) {
                                    ActiveEmitter newEm;
                                    newEm.props = props;
                                    newEm.duration = emitDuration;
                                    newEm.emissionRate = rate;
                                    newEm.timer = 0.0f;

                                    glm::vec3 pos = glm::vec3(0.0f);
                                    if (registry.HasComponent<TransformComponent>(e)) {
                                        pos = glm::vec3(registry.GetComponent<TransformComponent>(e).matrix[3]);
                                    }

                                    // 1. Assign the 3D position to our local copy
                                    newEm.props.position = pos;

                                    // 2. Pass the local copy (newEm.props) to the engine
                                    newEm.emitterId = scene.GetOrCreateSystem(newEm.props)->AddEmitter(newEm.props, rate);

                                    // 3. Save it to the component
                                    attached.emitters.push_back(newEm);
                                    };

                                // Dynamically load all presets from the library
                                auto presets = ParticleLibrary::GetAllPresets();
                                for (const auto& [name, props] : presets) {
                                    if (ImGui::MenuItem(name.c_str())) {
                                        attachFunc(props, 100.0f);
                                    }
                                }
                                ImGui::EndMenu();
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
                                ImGui::TextDisabled("Material");

                                // --- Dynamic Texture Dropdown ---
                                ImGui::Text("Texture:");
                                // Using a unique ID '##ObjTex' to prevent conflicts with the Properties Window
                                if (ImGui::BeginCombo("##ObjTexCombo", render.texturePath.c_str())) {
                                    for (const auto& texPath : m_AvailableTextures) {
                                        bool isSelected = (render.texturePath == texPath);
                                        if (ImGui::Selectable(texPath.c_str(), isSelected)) {
                                            render.texturePath = texPath;
                                        }
                                        if (isSelected) {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }

                                ImGui::SameLine();
                                if (ImGui::Button("Refresh##ObjTex")) {
                                    RefreshTextureList();
                                }

                                char texBuf[256];
                                strncpy_s(texBuf, render.texturePath.c_str(), sizeof(texBuf));
                                texBuf[sizeof(texBuf) - 1] = '\0';
                                if (ImGui::InputText("Manual Path / ID##Obj", texBuf, sizeof(texBuf))) {
                                    render.texturePath = std::string(texBuf);
                                }

                                // Procedural Texture Generator (Using BeginMenu instead of TreeNode)
                                if (ImGui::BeginMenu("Generate Procedural Texture##Obj")) {
                                    static char procName[64] = "custom_tex_1";
                                    static int procType = 1; // Default to Checkerboard
                                    static glm::vec4 color1(1.0f, 1.0f, 1.0f, 1.0f);
                                    static glm::vec4 color2(0.2f, 0.2f, 0.2f, 1.0f);
                                    static int cellSize = 32;

                                    ImGui::InputText("Name ID", procName, sizeof(procName));

                                    const char* procTypes[] = { "Solid Color", "Checkerboard", "Gradient (Vert)", "Gradient (Horiz)" };
                                    ImGui::Combo("Type", &procType, procTypes, IM_ARRAYSIZE(procTypes));

                                    ImGui::ColorEdit4("Color 1", &color1.x);
                                    if (procType > 0) { // Checkerboard and Gradients use a second color
                                        ImGui::ColorEdit4("Color 2", &color2.x);
                                    }
                                    if (procType == 1) { // Checkerboard uses cell size
                                        ImGui::InputInt("Cell Size", &cellSize);
                                    }

                                    if (ImGui::Button("Generate & Apply", ImVec2(-1, 0))) {
                                        ProceduralTextureRequest req;
                                        req.name = std::string(procName);
                                        req.type = static_cast<ProcTexType>(procType);
                                        req.color1 = color1;
                                        req.color2 = color2;
                                        req.cellSize = cellSize;
                                        m_TextureRequests.push_back(req);

                                        render.texturePath = req.name;
                                    }
                                    ImGui::EndMenu();
                                }
                            }

                            ImGui::Spacing();
                            if (ImGui::BeginMenu("Change Geometry...")) {
                                static int geoTypeIdx = 0;
                                const char* geoTypes[] = { "Model File", "Cube", "Sphere", "Bowl", "Terrain" };
                                ImGui::Combo("Shape Type", &geoTypeIdx, geoTypes, IM_ARRAYSIZE(geoTypes));

                                static std::string selectedModel = "";
                                if (geoTypeIdx == 0) { // Model File
                                    if (ImGui::BeginCombo("File", selectedModel.empty() ? "Select..." : selectedModel.c_str())) {
                                        for (const auto& mod : m_AvailableModels) {
                                            if (ImGui::Selectable(mod.c_str(), selectedModel == mod)) {
                                                selectedModel = mod;
                                            }
                                        }
                                        ImGui::EndCombo();
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button("Refresh##Models")) RefreshModelList();
                                }

                                ImGui::Spacing();
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                                if (ImGui::Button("Apply New Geometry", ImVec2(-1, 0))) {
                                    GeometryChangeRequest req;
                                    req.entity = e;
                                    req.type = geoTypes[geoTypeIdx];
                                    req.path = selectedModel;
                                    m_GeometryRequests.push_back(req);
                                }
                                ImGui::PopStyleColor();

                                ImGui::EndMenu();
                            }

                            // Display Attached Thermo Emitter Details
                            if (fireId != -1 || smokeId != -1) {
                                ImGui::Spacing();
                                ImGui::Separator();
                                ImGui::TextDisabled("Attached Thermodynamics");

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

                            ImGui::EndMenu();
                        }
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Particles")) {
                const auto& pSystems = scene.GetParticleSystems();

                struct EmitterDebugInfo {
                    int id;
                    std::string texName;
                    ParticleSystem::ParticleEmitter emitter;
                };

                std::vector<EmitterDebugInfo> allEmitters;

                for (const auto& sys : pSystems) {
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
                    std::sort(allEmitters.begin(), allEmitters.end(), [](const EmitterDebugInfo& a, const EmitterDebugInfo& b) {
                        return a.id < b.id;
                        });

                    Registry& registry = scene.GetRegistry();
                    const auto& entities = scene.GetRenderableEntities();

                    for (const auto& info : allEmitters) {
                        const auto& em = info.emitter;

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

                            ImGui::Spacing();
                            ImGui::TextDisabled("Attached Objects");
                            ImGui::Separator();

                            bool foundAttached = false;
                            for (Entity e : entities) {
                                bool isAttached = false;
                                std::string attachReason = "";

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

                                if (registry.HasComponent<DustCloudComponent>(e)) {
                                    auto& dust = registry.GetComponent<DustCloudComponent>(e);
                                    if (dust.emitterId == em.id) {
                                        isAttached = true;
                                        attachReason = "Dust";
                                    }
                                }

                                if (registry.HasComponent<AttachedEmitterComponent>(e)) {
                                    auto& attached = registry.GetComponent<AttachedEmitterComponent>(e);
                                    for (const auto& activeEm : attached.emitters) {
                                        if (activeEm.emitterId == em.id) {
                                            isAttached = true;
                                            attachReason = "Custom Emitter";
                                            break;
                                        }
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

                            ImGui::EndMenu();
                        }
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Cameras")) {
                Registry& registry = scene.GetRegistry();
                for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
                    if (!registry.HasComponent<CameraComponent>(e)) continue;

                    auto& cam = registry.GetComponent<CameraComponent>(e);

                    std::string baseCamName = registry.HasComponent<NameComponent>(e) ?
                        registry.GetComponent<NameComponent>(e).name : "Unnamed Camera";

                    std::string menuLabel = baseCamName;

                    if (cam.isActive) {
                        if (activeOrbitTarget != MAX_ENTITIES) {
                            std::string targetName = "Entity " + std::to_string(activeOrbitTarget);
                            if (registry.HasComponent<NameComponent>(activeOrbitTarget)) {
                                targetName = registry.GetComponent<NameComponent>(activeOrbitTarget).name;
                            }
                            menuLabel += " [VIEWING: " + targetName + "]";
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 1.0f, 1.0f));
                        }
                        else {
                            menuLabel += " [ACTIVE]";
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
                        }
                    }

                    menuLabel += "###CamMenu_" + std::to_string(e);

                    if (ImGui::BeginMenu(menuLabel.c_str())) {
                        if (cam.isActive) ImGui::PopStyleColor();

                        if (registry.HasComponent<TransformComponent>(e)) {
                            auto& transform = registry.GetComponent<TransformComponent>(e);
                            glm::vec3 pos = glm::vec3(transform.matrix[3]);

                            ImGui::TextDisabled("Spatial Data");
                            ImGui::Separator();
                            ImGui::Text("Position:    (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

                            glm::vec3 front = -glm::normalize(glm::vec3(transform.matrix[2]));
                            ImGui::Text("Front Vector: (%.2f, %.2f, %.2f)", front.x, front.y, front.z);

                            glm::vec3 up = glm::normalize(glm::vec3(transform.matrix[1]));
                            ImGui::Text("Up Vector:    (%.2f, %.2f, %.2f)", up.x, up.y, up.z);
                        }

                        ImGui::Spacing();
                        ImGui::TextDisabled("Orientation");
                        ImGui::Separator();
                        ImGui::Text("Yaw:   %.2f", cam.yaw);
                        ImGui::Text("Pitch: %.2f", cam.pitch);

                        ImGui::Spacing();
                        ImGui::TextDisabled("Lens Settings");
                        ImGui::Separator();
                        ImGui::Text("Field of View: %.1f deg", cam.fov);
                        ImGui::Text("Near Plane:    %.2f", cam.nearPlane);
                        ImGui::Text("Far Plane:     %.1f", cam.farPlane);
                        ImGui::Text("Aspect Ratio:  %.2f", cam.aspectRatio);

                        ImGui::Spacing();
                        ImGui::TextDisabled("Movement Stats");
                        ImGui::Separator();
                        ImGui::DragFloat("Move Speed", &cam.moveSpeed, 0.5f, 0.1f, 500.0f);
                        ImGui::DragFloat("Rotate Speed", &cam.rotateSpeed, 0.5f, 0.1f, 500.0f);

                        ImGui::Separator();

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

            if (ImGui::BeginMenu("Lights")) {
                Registry& registry = scene.GetRegistry();

                std::vector<Entity> activeLights;
                std::vector<Entity> inactiveLights;

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

                        const char* lightTypes[] = { "Sun / Directional", "Fire (Harsh Falloff)", "Standard Point", "Spotlight" };

                        int safeTypeIndex = (light.type >= 0 && light.type <= 3) ? light.type : 2;

                        if (ImGui::Combo("Light Type", &safeTypeIndex, lightTypes, 4)) {
                            light.type = safeTypeIndex;
                        }

                        if (light.type == 3) {
                            ImGui::Spacing();
                            ImGui::TextDisabled("Spotlight Settings");
                            ImGui::Separator();

                            if (ImGui::DragFloat3("Direction", &light.direction.x, 0.05f, -1.0f, 1.0f)) {
                                if (glm::length(light.direction) > 0.001f) {
                                    light.direction = glm::normalize(light.direction);
                                }
                            }

                            ImGui::SliderFloat("Cone Angle", &light.cutoffAngle, 1.0f, 90.0f, "%.1f deg");
                        }

                        const char* layerName = (light.layerMask & SceneLayers::INSIDE) ? "Inside" : "Outside";
                        ImGui::Text("Layer: %s", layerName);

                        ImGui::EndMenu();
                    }
                    };

                if (!activeLights.empty()) {
                    ImGui::TextDisabled("Active Lights");
                    ImGui::Separator();
                    for (Entity e : activeLights) {
                        drawLightMenu(e);
                    }
                }

                if (!inactiveLights.empty()) {
                    if (!activeLights.empty()) {
                        ImGui::Spacing();
                    }

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

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Simulation")) {
                std::string pauseLabel = m_IsPaused ? "Start Simulation  [Space]" : "Pause Simulation  [Space]";
                if (ImGui::Selectable(pauseLabel.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                    m_IsPaused = !m_IsPaused;
                }

                ImGui::Separator();

                ImGui::Text("Step Controls");
                ImGui::InputFloat("Step Size (s)", &m_StepSize, 0.001f, 0.01f, "%.4f");

                if (m_IsPaused) {
                    if (ImGui::Selectable("Execute Step  [F]", false, ImGuiSelectableFlags_DontClosePopups)) {
                        m_StepRequested = true;
                    }
                }
                else {
                    ImGui::TextDisabled("Execute Step  [F] (Pause first)");
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Restart Environment", "R")) {
                    m_RestartRequested = true;
                }

                ImGui::Separator();

                ImGui::Text("Simulation Speed (CTRL + CLICK to Type)");
                ImGui::SliderFloat("##speed", &m_TimeScale, 0.0f, 100.0f, "%.3fx", ImGuiSliderFlags_Logarithmic);

                ImGui::EndMenu();
            }

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

                bool useSimple = scene.IsUsingSimpleShadows();
                if (ImGui::Checkbox("Use Simple Shadows", &useSimple)) {
                    scene.ToggleSimpleShadows();
                }

                ImGui::Spacing();

                if (ImGui::Selectable("Cycle to Next Season", false, ImGuiSelectableFlags_DontClosePopups)) {
                    scene.NextSeason();
                }

                bool isPrecipitating = scene.IsPrecipitating();
                std::string weatherLabel = isPrecipitating ? "Stop Weather" : "Start Weather";
                if (ImGui::Selectable(weatherLabel.c_str(), false, ImGuiSelectableFlags_DontClosePopups)) {
                    scene.ToggleWeather();
                }

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

                ImGui::Spacing();
                ImGui::TextDisabled("Time of Day");
                ImGui::Separator();

                if (ImGui::Selectable("Set to Day", false, ImGuiSelectableFlags_DontClosePopups)) {
                    Registry& registry = scene.GetRegistry();
                    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
                        if (!registry.HasComponent<OrbitComponent>(e)) continue;

                        bool isSun = (registry.HasComponent<LightComponent>(e) && registry.GetComponent<LightComponent>(e).type == 0) ||
                            (registry.HasComponent<NameComponent>(e) && registry.GetComponent<NameComponent>(e).name.find("Sun") != std::string::npos);

                        bool isMoon = (registry.HasComponent<NameComponent>(e) && registry.GetComponent<NameComponent>(e).name.find("Moon") != std::string::npos);

                        if (isSun) {
                            registry.GetComponent<OrbitComponent>(e).currentAngle = glm::radians(90.0f);
                        }
                        else if (isMoon) {
                            registry.GetComponent<OrbitComponent>(e).currentAngle = glm::radians(270.0f);
                        }
                    }
                }

                if (ImGui::Selectable("Set to Night", false, ImGuiSelectableFlags_DontClosePopups)) {
                    Registry& registry = scene.GetRegistry();
                    for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
                        if (!registry.HasComponent<OrbitComponent>(e)) continue;

                        bool isSun = (registry.HasComponent<LightComponent>(e) && registry.GetComponent<LightComponent>(e).type == 0) ||
                            (registry.HasComponent<NameComponent>(e) && registry.GetComponent<NameComponent>(e).name.find("Sun") != std::string::npos);

                        bool isMoon = (registry.HasComponent<NameComponent>(e) && registry.GetComponent<NameComponent>(e).name.find("Moon") != std::string::npos);

                        if (isSun) {
                            registry.GetComponent<OrbitComponent>(e).currentAngle = glm::radians(270.0f);
                        }
                        else if (isMoon) {
                            registry.GetComponent<OrbitComponent>(e).currentAngle = glm::radians(90.0f);
                        }
                    }
                }

                ImGui::EndMenu();
            }

            std::string currentSceneName = m_SceneOptions.empty() ? "None" : m_SceneOptions[m_SelectedSceneIndex].name;
            std::string activeSceneStr = "Active Scene: " + currentSceneName;
            std::string fpsStr = std::to_string((int)(1.0f / deltaTime)) + " FPS";

            float spacing = 20.0f;
            float totalRightWidth = ImGui::CalcTextSize(activeSceneStr.c_str()).x +
                ImGui::CalcTextSize(fpsStr.c_str()).x +
                spacing + 40.0f;

            ImGui::SameLine(ImGui::GetWindowWidth() - totalRightWidth);

            ImGui::TextDisabled("Active Scene: ");
            ImGui::SameLine();
            ImGui::Text("%s", currentSceneName.c_str());

            ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(fpsStr.c_str()).x - 20.0f);
            ImGui::TextDisabled("%s", fpsStr.c_str());
            ImGui::EndMainMenuBar();
        }

        if (m_ShowControlsWindow) {
            ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Input Controls", &m_ShowControlsWindow)) {

                if (m_DisplayBindings.empty()) {
                    ImGui::TextDisabled("No bindings loaded.");
                }
                else {
                    if (ImGui::BeginTable("ControlsTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {

                        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.6f);
                        ImGui::TableSetupColumn("Key Bound", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                        ImGui::TableHeadersRow();

                        for (const auto& bind : m_DisplayBindings) {
                            ImGui::TableNextRow();

                            ImGui::TableNextColumn();
                            ImGui::Text("%s", bind.first.c_str());

                            ImGui::TableNextColumn();
                            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%s", bind.second.c_str());
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::End();
        }

        if (m_ShowEntityPropertiesWindow) {
            ImGui::SetNextWindowSize(ImVec2(500, 700), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Entity Properties", &m_ShowEntityPropertiesWindow)) {

                Registry& registry = scene.GetRegistry();
                Entity count = registry.GetEntityCount();

                // Helper lambda to cleanly draw the menu items to add new components
                auto addMenuItem = [&](auto type_dummy, const char* name, Entity e) {
                    using T = typename std::remove_pointer<decltype(type_dummy)>::type;
                    if (!registry.HasComponent<T>(e)) {
                        if (ImGui::MenuItem(name)) {
                            registry.AddComponent<T>(e, T{});
                        }
                    }
                    };

                for (Entity e = 0; e < count; ++e) {
                    std::string entityName = "Entity " + std::to_string(e);
                    if (registry.HasComponent<NameComponent>(e)) {
                        entityName += " (" + registry.GetComponent<NameComponent>(e).name + ")";
                    }

                    ImGui::PushID(e);
                    if (ImGui::CollapsingHeader(entityName.c_str())) {
                        ImGui::Indent();

                        ImGui::Spacing();
                        if (ImGui::Button("View Object", ImVec2(-1, 0))) {
                            m_ViewRequested = e;
                        }
                        ImGui::Spacing();

                        ImGui::TextDisabled("Attached Components");
                        ImGui::Separator();

                        // --- 1. Name Component ---
                        if (registry.HasComponent<NameComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("NameComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Name")) registry.RemoveComponent<NameComponent>(e);

                            if (open && registry.HasComponent<NameComponent>(e)) {
                                auto& comp = registry.GetComponent<NameComponent>(e);
                                char buf[256];
                                strncpy_s(buf, comp.name.c_str(), sizeof(buf));
                                buf[sizeof(buf) - 1] = '\0';
                                if (ImGui::InputText("Name", buf, sizeof(buf))) {
                                    comp.name = std::string(buf);
                                }
                                ImGui::TreePop();
                            }
                        }

                        // --- 2. Transform Component ---
                        if (registry.HasComponent<TransformComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("TransformComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Transform")) registry.RemoveComponent<TransformComponent>(e);

                            if (open && registry.HasComponent<TransformComponent>(e)) {
                                auto& comp = registry.GetComponent<TransformComponent>(e);

                                bool modified = false;

                                if (ImGui::DragFloat3("Position", &comp.position.x, 0.1f)) modified = true;
                                if (ImGui::DragFloat3("Rotation", &comp.rotation.x, 1.0f)) modified = true;

                                ImGui::Spacing();

                                // --- NEW: Uniform Scaling ---
                                // Grab the X scale as the baseline for the uniform slider
                                float uniformScale = comp.scale.x;
                                if (ImGui::DragFloat("Uniform Scale", &uniformScale, 0.05f)) {
                                    // If dragged, apply to all 3 axes instantly
                                    comp.scale = glm::vec3(uniformScale);
                                    modified = true;
                                }

                                // Keep the individual axes slider for precise stretching
                                if (ImGui::DragFloat3("Axis Scale", &comp.scale.x, 0.05f)) modified = true;

                                // Rebuild the matrix if anything moved
                                if (modified) {
                                    comp.UpdateMatrix();
                                }

                                ImGui::TreePop();
                            }
                        }

                        // --- 3. Render Component ---
                        if (registry.HasComponent<RenderComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("RenderComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Render")) registry.RemoveComponent<RenderComponent>(e);

                            if (open && registry.HasComponent<RenderComponent>(e)) {
                                auto& comp = registry.GetComponent<RenderComponent>(e);
                                ImGui::Checkbox("Visible", &comp.visible);
                                ImGui::Checkbox("Casts Shadow", &comp.castsShadow);
                                ImGui::Checkbox("Receives Shadows", &comp.receiveShadows);

                                const char* modes[] = { "None", "Phong", "Gouraud", "Flat", "Wireframe" };
                                ImGui::Combo("Shading Mode", &comp.shadingMode, modes, IM_ARRAYSIZE(modes));

                                // Direct Layer Mask Editor
                                ImGui::InputInt("Layer Mask", &comp.layerMask);

                                // --- Dynamic Texture Dropdown ---
                                ImGui::Text("Texture:");
                                if (ImGui::BeginCombo("##TextureCombo", comp.texturePath.c_str())) {
                                    for (const auto& texPath : m_AvailableTextures) {
                                        bool isSelected = (comp.texturePath == texPath);
                                        if (ImGui::Selectable(texPath.c_str(), isSelected)) {
                                            comp.texturePath = texPath;
                                        }
                                        if (isSelected) {
                                            ImGui::SetItemDefaultFocus();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }

                                ImGui::SameLine();
                                if (ImGui::Button("Refresh##Tex")) {
                                    RefreshTextureList();
                                }

                                // We keep the Manual input so you can still type Procedural Texture Names (like "custom_tex_1")
                                char texBuf[256];
                                strncpy_s(texBuf, comp.texturePath.c_str(), sizeof(texBuf));
                                texBuf[sizeof(texBuf) - 1] = '\0';
                                if (ImGui::InputText("Manual Path / ID", texBuf, sizeof(texBuf))) {
                                    comp.texturePath = std::string(texBuf);
                                }

                                // Procedural Texture Generator
                                if (ImGui::TreeNode("Generate Procedural Texture")) {
                                    static char procName[64] = "custom_tex_1";
                                    static int procType = 1; // Default to Checkerboard
                                    static glm::vec4 color1(1.0f, 1.0f, 1.0f, 1.0f);
                                    static glm::vec4 color2(0.2f, 0.2f, 0.2f, 1.0f);
                                    static int cellSize = 32;

                                    ImGui::InputText("Name ID", procName, sizeof(procName));

                                    const char* procTypes[] = { "Solid Color", "Checkerboard", "Gradient (Vert)", "Gradient (Horiz)" };
                                    ImGui::Combo("Type", &procType, procTypes, IM_ARRAYSIZE(procTypes));

                                    ImGui::ColorEdit4("Color 1", &color1.x);
                                    if (procType > 0) { // Checkerboard and Gradients use a second color
                                        ImGui::ColorEdit4("Color 2", &color2.x);
                                    }
                                    if (procType == 1) { // Checkerboard uses cell size
                                        ImGui::InputInt("Cell Size", &cellSize);
                                    }

                                    if (ImGui::Button("Generate & Apply", ImVec2(-1, 0))) {
                                        // 1. Queue the request for the main loop
                                        ProceduralTextureRequest req;
                                        req.name = std::string(procName);
                                        req.type = static_cast<ProcTexType>(procType);
                                        req.color1 = color1;
                                        req.color2 = color2;
                                        req.cellSize = cellSize;
                                        m_TextureRequests.push_back(req);

                                        // 2. Instantly update the entity to use the new texture name
                                        comp.texturePath = req.name;
                                    }
                                    ImGui::TreePop();
                                }
                                ImGui::TreePop();

                                // --- CHANGE GEOMETRY ---
                                if (ImGui::TreeNode("Change Geometry")) {
                                    static int geoTypeIdx = 0;
                                    const char* geoTypes[] = { "Model File", "Cube", "Sphere", "Bowl", "Terrain" };
                                    ImGui::Combo("Shape Type", &geoTypeIdx, geoTypes, IM_ARRAYSIZE(geoTypes));

                                    static std::string selectedModel = "";
                                    if (geoTypeIdx == 0) { // Model File
                                        if (ImGui::BeginCombo("File", selectedModel.empty() ? "Select..." : selectedModel.c_str())) {
                                            for (const auto& mod : m_AvailableModels) {
                                                if (ImGui::Selectable(mod.c_str(), selectedModel == mod)) {
                                                    selectedModel = mod;
                                                }
                                            }
                                            ImGui::EndCombo();
                                        }
                                        ImGui::SameLine();
                                        if (ImGui::Button("Refresh##ModelsProp")) RefreshModelList();
                                    }

                                    ImGui::Spacing();
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                                    if (ImGui::Button("Apply New Geometry", ImVec2(-1, 0))) {
                                        // Send the request to the main loop to handle safe GPU buffer replacement
                                        GeometryChangeRequest req;
                                        req.entity = e;
                                        req.type = geoTypes[geoTypeIdx];
                                        req.path = selectedModel;
                                        m_GeometryRequests.push_back(req);
                                    }
                                    ImGui::PopStyleColor();

                                    ImGui::TreePop();
                                }
                            }
                        }

                        // --- 4. Light Component ---
                        if (registry.HasComponent<LightComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("LightComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Light")) registry.RemoveComponent<LightComponent>(e);

                            if (open && registry.HasComponent<LightComponent>(e)) {
                                auto& comp = registry.GetComponent<LightComponent>(e);
                                ImGui::ColorEdit3("Color", &comp.color.x, ImGuiColorEditFlags_Float);
                                ImGui::DragFloat("Intensity", &comp.intensity, 0.1f, 0.0f, 1000.0f);

                                const char* lightTypes[] = { "Sun", "Fire", "Point", "Spotlight" };
                                ImGui::Combo("Type", &comp.type, lightTypes, IM_ARRAYSIZE(lightTypes));

                                if (comp.type == 3) { // Spotlight
                                    ImGui::DragFloat3("Direction", &comp.direction.x, 0.05f, -1.0f, 1.0f);
                                    ImGui::SliderFloat("Cutoff Angle", &comp.cutoffAngle, 1.0f, 90.0f);
                                }
                                ImGui::TreePop();
                            }
                        }

                        // --- 5. Orbit Component ---
                        if (registry.HasComponent<OrbitComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("OrbitComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Orbit")) registry.RemoveComponent<OrbitComponent>(e);

                            if (open && registry.HasComponent<OrbitComponent>(e)) {
                                auto& comp = registry.GetComponent<OrbitComponent>(e);
                                ImGui::Checkbox("Is Orbiting", &comp.isOrbiting);
                                ImGui::DragFloat3("Center", &comp.center.x, 0.1f);
                                ImGui::DragFloat("Radius", &comp.radius, 0.1f);
                                ImGui::DragFloat("Speed", &comp.speed, 0.01f);
                                ImGui::DragFloat3("Axis", &comp.axis.x, 0.1f);
                                ImGui::DragFloat("Current Angle", &comp.currentAngle, 0.01f);
                                ImGui::TreePop();
                            }
                        }

                        // --- 6. Thermo Component ---
                        if (registry.HasComponent<ThermoComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("ThermoComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Thermo")) registry.RemoveComponent<ThermoComponent>(e);

                            if (open && registry.HasComponent<ThermoComponent>(e)) {
                                auto& comp = registry.GetComponent<ThermoComponent>(e);
                                ImGui::Checkbox("Is Flammable", &comp.isFlammable);
                                ImGui::Checkbox("Can Burnout", &comp.canBurnout);
                                ImGui::DragFloat("Current Temp", &comp.currentTemp, 1.0f);
                                ImGui::DragFloat("Ignition Threshold", &comp.ignitionThreshold, 1.0f);
                                ImGui::DragFloat("Burn Timer", &comp.burnTimer, 0.1f);

                                // Show human-readable state
                                const char* states[] = { "NORMAL", "HEATING", "BURNING", "BURNT_OUT" };
                                int stateIdx = (int)comp.state;
                                if (stateIdx >= 0 && stateIdx <= 3) {
                                    ImGui::Text("State: %s", states[stateIdx]);
                                }
                                else {
                                    ImGui::Text("State: %d", stateIdx);
                                }

                                // --- Show Hidden Fire Data & Controls ---
                                ImGui::Spacing();
                                if (comp.state == ObjectState::BURNING) {
                                    ImGui::TextDisabled("Active Fire Data");
                                    ImGui::Separator();
                                    ImGui::Text("Fire Emitter ID: %d", comp.fireEmitterId);
                                    ImGui::Text("Smoke Emitter ID: %d", comp.smokeEmitterId);
                                    ImGui::Text("Light Entity ID: %d", comp.fireLightEntity);

                                    ImGui::Spacing();
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
                                    if (ImGui::Button("Extinguish Fire", ImVec2(-1, 0))) {
                                        scene.StopObjectFire(e);
                                    }
                                    ImGui::PopStyleColor();
                                }
                                else if (comp.isFlammable) {
                                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
                                    if (ImGui::Button("Ignite Object", ImVec2(-1, 0))) {
                                        scene.Ignite(e);
                                    }
                                    ImGui::PopStyleColor();
                                }

                                ImGui::TreePop();
                            }
                        }

                        // --- Attached Emitter Component ---
                        if (registry.HasComponent<AttachedEmitterComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("AttachedEmitterComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Emitter")) registry.RemoveComponent<AttachedEmitterComponent>(e);

                            if (open && registry.HasComponent<AttachedEmitterComponent>(e)) {
                                auto& comp = registry.GetComponent<AttachedEmitterComponent>(e);

                                ImGui::Text("Active Emitters: %d", (int)comp.emitters.size());

                                for (size_t i = 0; i < comp.emitters.size(); ++i) {
                                    auto& em = comp.emitters[i];
                                    ImGui::PushID((int)i);
                                    if (ImGui::TreeNodeEx(("Emitter ID: " + std::to_string(em.emitterId)).c_str())) {
                                        ImGui::DragFloat("Emission Rate", &em.emissionRate, 1.0f, 0.0f, 1000.0f);
                                        ImGui::DragFloat("Duration (-1 = Inf)", &em.duration, 0.1f);
                                        ImGui::Text("Timer: %.2f", em.timer);
                                        ImGui::TreePop();
                                    }
                                    ImGui::PopID();
                                }

                                if (ImGui::BeginMenu("Attach New Emitter...")) {
                                    static float emitDuration = -1.0f;
                                    ImGui::InputFloat("Duration (s)", &emitDuration);
                                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set to -1 for Infinite");
                                    ImGui::Separator();

                                    auto attachFunc = [&](ParticleProps props, float rate) {
                                        ActiveEmitter newEm;
                                        newEm.props = props;
                                        newEm.duration = emitDuration;
                                        newEm.emissionRate = rate;
                                        newEm.timer = 0.0f;

                                        glm::vec3 pos = glm::vec3(0.0f);
                                        if (registry.HasComponent<TransformComponent>(e)) {
                                            pos = glm::vec3(registry.GetComponent<TransformComponent>(e).matrix[3]);
                                        }

                                        // We update the position on our copy
                                        newEm.props.position = pos;

                                        // Use the copy to register the emitter!
                                        newEm.emitterId = scene.GetOrCreateSystem(newEm.props)->AddEmitter(newEm.props, rate);

                                        // Note: use `attached.emitters.push_back(newEm);` if in the Objects menu
                                        // or `comp.emitters.push_back(newEm);` if in the Entity Properties menu
                                        comp.emitters.push_back(newEm);
                                     };

                                    auto presets = ParticleLibrary::GetAllPresets();
                                    for (const auto& [name, props] : presets) {
                                        if (ImGui::MenuItem(name.c_str())) {
                                            attachFunc(props, 100.0f);
                                        }
                                    }

                                    ImGui::EndMenu();
                                }
                                ImGui::TreePop();
                            }
                        }

                        // --- 7. Camera Component ---
                        if (registry.HasComponent<CameraComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("CameraComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Camera")) registry.RemoveComponent<CameraComponent>(e);

                            if (open && registry.HasComponent<CameraComponent>(e)) {
                                auto& comp = registry.GetComponent<CameraComponent>(e);
                                ImGui::Checkbox("Is Active", &comp.isActive);
                                ImGui::DragFloat("FOV", &comp.fov, 1.0f, 10.0f, 150.0f);
                                ImGui::DragFloat("Move Speed", &comp.moveSpeed, 0.5f);
                                ImGui::DragFloat("Rotate Speed", &comp.rotateSpeed, 0.5f);
                                ImGui::DragFloat("Yaw", &comp.yaw, 1.0f);
                                ImGui::DragFloat("Pitch", &comp.pitch, 1.0f);
                                ImGui::TreePop();
                            }
                        }

                        // --- 8. Collider Component ---
                        if (registry.HasComponent<ColliderComponent>(e)) {
                            bool open = ImGui::TreeNodeEx("ColliderComponent", ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                            if (ImGui::Button("Remove##Collider")) registry.RemoveComponent<ColliderComponent>(e);

                            if (open && registry.HasComponent<ColliderComponent>(e)) {
                                auto& comp = registry.GetComponent<ColliderComponent>(e);
                                ImGui::Checkbox("Has Collision", &comp.hasCollision);
                                ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.0f, 100.0f);
                                ImGui::DragFloat("Height", &comp.height, 0.1f, 0.0f, 100.0f);
                                ImGui::TreePop();
                            }
                        }

                        

                        ImGui::Spacing();

                        // 2. Dropdown to Add New Components
                        if (ImGui::BeginMenu("Add Component...")) {
                            addMenuItem((NameComponent*)nullptr, "NameComponent", e);
                            addMenuItem((TransformComponent*)nullptr, "TransformComponent", e);
                            addMenuItem((RenderComponent*)nullptr, "RenderComponent", e);
                            addMenuItem((OrbitComponent*)nullptr, "OrbitComponent", e);
                            addMenuItem((ThermoComponent*)nullptr, "ThermoComponent", e);
                            addMenuItem((ColliderComponent*)nullptr, "ColliderComponent", e);
                            addMenuItem((LightComponent*)nullptr, "LightComponent", e);
                            addMenuItem((CameraComponent*)nullptr, "CameraComponent", e);
                            addMenuItem((AttachedEmitterComponent*)nullptr, "AttachedEmitterComponent", e);
                            addMenuItem((EnvironmentComponent*)nullptr, "EnvironmentComponent", e);
                            addMenuItem((DustCloudComponent*)nullptr, "DustCloudComponent", e);

                            ImGui::EndMenu();
                        }

                        ImGui::Unindent();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::End();
        }

        return sceneToLoad;
    }
}