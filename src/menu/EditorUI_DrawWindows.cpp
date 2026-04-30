#include "EditorUI.h"
#include "imgui.h"
#include "../rendering/ParticleLibrary.h"
#include "../systems/CameraSystem.h"
#include "../systems/ObjectSpawnerSystem.h"
#include <algorithm>
#include <random>
#include <unordered_set>

void EditorUI::DrawPostMainMenuSection(Scene& scene, Entity entityToDelete) {
if (entityToDelete != MAX_ENTITIES) {
    scene.DeleteEntity(entityToDelete);

    if (m_ViewRequested == entityToDelete) {
        m_ViewRequested = MAX_ENTITIES;
    }

    for (auto& wnd : m_PropertyWindows) {
        if (wnd.selectedEntity == entityToDelete) {
            wnd.selectedEntity = MAX_ENTITIES;
        }
    }
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

if (m_Profiler.showProfiler) {
    ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("UI Profiler", &m_Profiler.showProfiler)) {
        ImGui::Text("Total UI Draw: %.3f ms", m_Profiler.totalTime);
        ImGui::Separator();
        ImGui::Text("Main Menu Bar: %.3f ms", m_Profiler.drawMainMenuTime);
        ImGui::Text("Property Windows: %.3f ms", m_Profiler.drawWindowsTime);
        
        ImGui::Separator();
        ImGui::TextDisabled("Vulkan Stats:");
        ImGui::Text("Target FPS: %s", m_FpsCapEnabled ? std::to_string(m_MaxFps).c_str() : "Unlimited");
        ImGui::Text("VSync: %s", m_VSyncEnabled ? "On" : "Off");
        
        if (ImGui::Button("Reset All Timers")) {
            m_Profiler.totalTime = 0;
            m_Profiler.drawMainMenuTime = 0;
            m_Profiler.drawWindowsTime = 0;
        }
    }
    ImGui::End();
}

}

void EditorUI::DrawPropertyWindowsSection(Scene& scene, Entity& entityToDelete) {
    // ADD THIS LINE
    std::vector<Entity> popoutRequests;

    auto isLayerUsed = [&](int bit) -> bool {
        if (bit == 0) return true; // Always keep Base World visible

        const int bitMask = (1 << bit);
        Registry& reg = scene.GetRegistry();
        const Entity count = reg.GetEntityCount();

        for (Entity e = 0; e < count; ++e) {
            if (reg.HasComponent<LayerRegionComponent>(e) &&
                reg.GetComponent<LayerRegionComponent>(e).assignedLayerBit == bit) {
                return true;
            }

            if (reg.HasComponent<RenderComponent>(e)) {
                const auto& rc = reg.GetComponent<RenderComponent>(e);
                if ((rc.layerMask & bitMask) != 0) return true;
                if ((rc.onlyInRegionMask & bitMask) != 0) return true;
            }

            if (reg.HasComponent<LightComponent>(e)) {
                if ((reg.GetComponent<LightComponent>(e).layerMask & bitMask) != 0) return true;
            }

            if (reg.HasComponent<CameraComponent>(e)) {
                const auto& cc = reg.GetComponent<CameraComponent>(e);
                if ((cc.viewMask & bitMask) != 0 || (cc.insideRegionMask & bitMask) != 0) return true;
            }
        }

        return false;
        };

    auto drawLayerCheckboxes = [&](const char* label, int& visibleMask, int& onlyMask) {
        ImGui::TextDisabled("%s", label);

        auto getMode = [&](int bit) -> int {
            const int bitMask = (1 << bit);
            const bool visible = (visibleMask & bitMask) != 0;
            const bool only = (onlyMask & bitMask) != 0;

            if (only) return 2;     // Only in region
            if (visible) return 1;  // Enabled
            return 0;               // Disabled
            };

        auto setMode = [&](int bit, int mode) {
            const int bitMask = (1 << bit);

            // Clear this bit from both masks first
            visibleMask &= ~bitMask;
            onlyMask &= ~bitMask;

            switch (mode) {
            case 1: // Enabled
                visibleMask |= bitMask;
                break;
            case 2: // Only display in region
                visibleMask |= bitMask;
                onlyMask |= bitMask;
                break;
            default: // Disabled
                break;
            }
            };

        std::vector<int> visibleBits;
        const int maxVisibleLayer = std::max(1, SceneLayers::ActiveLayerCount);
        visibleBits.reserve(maxVisibleLayer);

        static std::vector<bool> layerUsedCache;
        static uint64_t lastFrameCount = 0;
        // Simple way to refresh once per frame (assuming this is called within a frame context)
        // For simplicity here, we'll just cache it for the duration of the section call
        
        for (int i = 0; i < maxVisibleLayer; ++i) {
            const int bitMask = (1 << i);
            const bool currentlySet = (visibleMask & bitMask) != 0 || (onlyMask & bitMask) != 0;

            bool used = currentlySet;
            if (!used) {
                used = isLayerUsed(i);
            }

            if (used) {
                visibleBits.push_back(i);
            }
        }

        static const char* modeItems[] = {
            "Disabled",
            "Enabled",
            "Only Display In Region"
        };

        // Scope table IDs by label so multiple property windows don't collide
        ImGui::PushID(label);
        if (ImGui::BeginTable("LayerRulesTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (int bit : visibleBits) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%s", SceneLayers::LayerNames[bit].c_str());

                ImGui::TableNextColumn();
                int mode = getMode(bit);

                ImGui::PushID(bit);
                if (ImGui::Combo("##Mode", &mode, modeItems, IM_ARRAYSIZE(modeItems))) {
                    setMode(bit, mode);
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
        ImGui::PopID();

        ImGui::Spacing();
    };

// --- MULTIPLE ENTITY PROPERTIES WINDOWS ---
for (auto it = m_PropertyWindows.begin(); it != m_PropertyWindows.end(); ) {
    if (!it->isOpen) {
        it = m_PropertyWindows.erase(it);
        continue;
    }

    ImGui::SetNextWindowSize(ImVec2(850, 600), ImGuiCond_FirstUseEver);
    
    // REPLACE the std::string windowTitle line with this block:
    std::string windowTitle = "Entity Properties (Window " + std::to_string(it->id) + ")###PropWin" + std::to_string(it->id);
    if (!it->showList && it->selectedEntity != MAX_ENTITIES && it->selectedEntity < scene.GetRegistry().GetEntityCount()) {
        std::string eName = "Entity " + std::to_string(it->selectedEntity);
        if (scene.GetRegistry().HasComponent<NameComponent>(it->selectedEntity)) {
            eName = scene.GetRegistry().GetComponent<NameComponent>(it->selectedEntity).name;
        }
        windowTitle = eName + " Properties###PropWin" + std::to_string(it->id);
    }
    // END REPLACE

    if (ImGui::Begin(windowTitle.c_str(), &it->isOpen)) {
        Registry& registry = scene.GetRegistry();
        Entity count = registry.GetEntityCount();

        // Wrap the top bar buttons so they only show on the main window
        if (!it->isPopout) {
            // Top bar for the window (Retract button)
            if (ImGui::Button(it->showList ? "<< Hide Entity List" : ">> Show Entity List")) {
                it->showList = !it->showList;
            }
            ImGui::Separator();
            ImGui::Spacing();

            // Create New Entity button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            if (ImGui::Button("Create New Entity", ImVec2(-1, 0))) {
                static int newEntityCount = 1;
                std::string name = "NewEntity_" + std::to_string(newEntityCount++);
                // Spawn a default 1x1 cube
                scene.AddCube(name, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), "");
                // Ensure at least one properties window exists
                if (m_PropertyWindows.empty()) {
                    m_PropertyWindows.push_back({ m_NextPropertyWindowId++, MAX_ENTITIES, true, true, false });
                }
            }
            ImGui::PopStyleColor(3);

            ImGui::Spacing();
        }

        auto isDebugVisualEntity = [&](Entity e) -> bool {
            if (!registry.HasComponent<RenderComponent>(e)) {
                return false;
            }

            const auto& render = registry.GetComponent<RenderComponent>(e);
            return render.geometryName == "spring_visual" || render.geometryName == "path_visual" || render.geometryName == "spawner_visual";
            };

        auto hasVisibleEntityData = [&](Entity e) -> bool {
            if (isDebugVisualEntity(e)) {
                return false;
            }

            return registry.HasComponent<NameComponent>(e)
                || registry.HasComponent<TransformComponent>(e)
                || registry.HasComponent<RenderComponent>(e)
                || registry.HasComponent<LightComponent>(e)
                || registry.HasComponent<CameraComponent>(e)
                || registry.HasComponent<PhysicsComponent>(e)
                || registry.HasComponent<ColliderComponent>(e)
                || registry.HasComponent<ThermoComponent>(e)
                || registry.HasComponent<EnvironmentComponent>(e)
                || registry.HasComponent<OrbitComponent>(e)
                || registry.HasComponent<LayerRegionComponent>(e)
                || registry.HasComponent<ObjectSpawnerComponent>(e)
                || registry.HasComponent<PathAnimationComponent>(e)
                || registry.HasComponent<DustCloudComponent>(e)
                || registry.HasComponent<DespawnerComponent>(e)
                || registry.HasComponent<SpawnedFromSpawnerComponent>(e)
                || registry.HasComponent<AttachedEmitterComponent>(e);
            };

        if (it->selectedEntity != MAX_ENTITIES &&
            (it->selectedEntity >= count ||
                (!hasVisibleEntityData(it->selectedEntity) && !isDebugVisualEntity(it->selectedEntity)))) {
            it->selectedEntity = MAX_ENTITIES;
        }

        // --- LEFT PANE: Selectable Entity List (Retractable) ---
        if (it->showList) {
            ImGui::BeginChild("EntityListPane", ImVec2(250, 0), true);

            int springVisualCount = 0;
            int pathVisualCount = 0;
            int spawnerVisualCount = 0;

            std::unordered_set<Entity> clothParticles;
            for (Entity e = 0; e < count; ++e) {
                if (registry.HasComponent<ClothComponent>(e)) {
                    const auto& comp = registry.GetComponent<ClothComponent>(e);
                    clothParticles.insert(comp.particles.begin(), comp.particles.end());
                }
            }

            for (Entity e = 0; e < count; ++e) {
                if (isDebugVisualEntity(e)) {
                    const auto& render = registry.GetComponent<RenderComponent>(e);
                    if (render.geometryName == "spring_visual") {
                        ++springVisualCount;
                    }
                    else if (render.geometryName == "path_visual") {
                        ++pathVisualCount;
                    }
                    else if (render.geometryName == "spawner_visual") {
                        ++spawnerVisualCount;
                    }
                    continue;
                }

                if (!hasVisibleEntityData(e)) {
                    continue;
                }
                
                if (clothParticles.count(e) > 0) {
                    continue; // Skip rendering particles here, they will be rendered under the cloth entity
                }

                std::string entityName = "Entity " + std::to_string(e);
                if (registry.HasComponent<NameComponent>(e)) {
                    entityName = registry.GetComponent<NameComponent>(e).name + " (ID: " + std::to_string(e) + ")";
                }

                if (registry.HasComponent<ClothComponent>(e)) {
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
                    if (it->selectedEntity == e) flags |= ImGuiTreeNodeFlags_Selected;
                    
                    bool expanded = ImGui::TreeNodeEx((void*)(intptr_t)e, flags, "%s", entityName.c_str());
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        it->selectedEntity = e;
                    }

                    if (expanded) {
                        const auto& comp = registry.GetComponent<ClothComponent>(e);
                        for(Entity p : comp.particles) {
                            std::string pName = "  Particle " + std::to_string(p);
                            bool pSelected = (it->selectedEntity == p);
                            if (ImGui::Selectable(pName.c_str(), pSelected)) {
                                it->selectedEntity = p;
                            }
                        }
                        ImGui::TreePop();
                    }
                } else {
                    bool isSelected = (it->selectedEntity == e);
                    if (ImGui::Selectable(entityName.c_str(), isSelected)) {
                        it->selectedEntity = e;
                    }
                }
            }

            if (springVisualCount > 0 || pathVisualCount > 0 || spawnerVisualCount > 0) {
                ImGui::Separator();
                if (ImGui::TreeNode("Visualization Helpers")) {
                    ImGui::BulletText("Spring visuals: %d", springVisualCount);
                    ImGui::BulletText("Path visuals: %d", pathVisualCount);
                    ImGui::BulletText("Spawner visuals: %d", spawnerVisualCount);
                    ImGui::TreePop();
                }
            }

            ImGui::EndChild();
            ImGui::SameLine();
        }

        // --- RIGHT PANE: Entity Properties Menu ---
        ImGui::BeginChild("EntityDetailsPane", ImVec2(0, 0), true);

        if (it->selectedEntity != MAX_ENTITIES && it->selectedEntity < count) {
            Entity e = it->selectedEntity;

            std::string headerName = "Viewing: Entity " + std::to_string(e);
            if (registry.HasComponent<NameComponent>(e)) {
                headerName += " (" + registry.GetComponent<NameComponent>(e).name + ")";
            }

            // --- Show incoming spring connections (reverse lookup) ---
            bool incomingSpringHeaderDrawn = false;
            for (Entity s = 0; s < registry.GetEntityCount(); ++s) {
                if (!registry.HasComponent<SpringComponent>(s)) continue;

                auto& sp = registry.GetComponent<SpringComponent>(s);
                if (!sp.isAttachedToEntity) continue;

                auto itConn = std::find(sp.connectedEntities.begin(), sp.connectedEntities.end(), e);
                if (itConn == sp.connectedEntities.end()) continue;

                if (!incomingSpringHeaderDrawn) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[ Driven by External Springs ]");
                    incomingSpringHeaderDrawn = true;
                }

                std::string anchorName = "Entity " + std::to_string(s);
                if (registry.HasComponent<NameComponent>(s)) {
                    anchorName = registry.GetComponent<NameComponent>(s).name;
                }

                ImGui::PushID(static_cast<int>(s));
                ImGui::BulletText("Anchored to: %s", anchorName.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("View Anchor")) {
                    it->selectedEntity = s;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Disconnect")) {
                    sp.connectedEntities.erase(itConn);
                }
                ImGui::PopID();
            }

            ImGui::Separator();
            ImGui::Spacing();

            // WRAP THE POP-OUT BUTTON IN THIS CHECK
            if (!it->isPopout) {
                if (ImGui::Button("Pop-out to New Window", ImVec2(-1, 0))) {
                    popoutRequests.push_back(e);
                }
                ImGui::Spacing();
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("Delete Entity", ImVec2(-1, 0))) {
                entityToDelete = e;
            }
            ImGui::PopStyleColor(3);

            const bool isCamera = registry.HasComponent<CameraComponent>(e);
            const bool canFocusObject = registry.HasComponent<TransformComponent>(e);

            if (isCamera) {
                if (ImGui::Button("Switch to this Camera", ImVec2(-1, 0))) {
                    for (Entity camTarget = 0; camTarget < registry.GetEntityCount(); ++camTarget) {
                        if (registry.HasComponent<CameraComponent>(camTarget)) {
                            registry.GetComponent<CameraComponent>(camTarget).isActive = (camTarget == e);
                        }
                    }
                }
            }
            else {
                if (!canFocusObject) ImGui::BeginDisabled();
                if (ImGui::Button("Focus Camera on Object", ImVec2(-1, 0)) && canFocusObject) {
                    m_ViewRequested = e;
                }
                if (!canFocusObject) ImGui::EndDisabled();
            }

            ImGui::Spacing();

            // Helper lambda to cleanly draw the menu items to add new components
            auto addMenuItem = [&](auto type_dummy, const char* name, Entity ent) {
                using T = typename std::remove_pointer<decltype(type_dummy)>::type;
                if (!registry.HasComponent<T>(ent)) {
                    if (ImGui::MenuItem(name)) {
                        registry.AddComponent<T>(ent, T{});
                    }
                }
            };

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

                    float uniformScale = comp.scale.x;
                    if (ImGui::DragFloat("Uniform Scale", &uniformScale, 0.05f)) {
                        comp.scale = glm::vec3(uniformScale);
                        modified = true;
                    }
                    if (ImGui::DragFloat3("Axis Scale", &comp.scale.x, 0.05f)) modified = true;

                    if (modified) comp.UpdateMatrix();

                    ImGui::TreePop();
                }
            }

            // --- 3. Render Component ---
            if (registry.HasComponent<RenderComponent>(e)) {
                bool open = ImGui::TreeNodeEx("RenderComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Render")) scene.RemoveRenderComponent(e);

                if (open && registry.HasComponent<RenderComponent>(e)) {
                    auto& comp = registry.GetComponent<RenderComponent>(e);
                    ImGui::Checkbox("Visible", &comp.visible);
                    ImGui::Checkbox("Casts Shadow", &comp.castsShadow);
                    ImGui::Checkbox("Receives Shadows", &comp.receiveShadows);

                    const char* modes[] = { "None", "Phong", "Gouraud", "Flat", "Wireframe" };
                    ImGui::Combo("Shading Mode", &comp.shadingMode, modes, IM_ARRAYSIZE(modes));

                    drawLayerCheckboxes("Layer Visibility Rules", comp.layerMask, comp.onlyInRegionMask);

                    std::string textureNodeLabel =
                        "Current Texture: " + comp.texturePath + "###TextureNode_" +
                        std::to_string(it->id) + "_" + std::to_string(e);

                    if (ImGui::TreeNode(textureNodeLabel.c_str())) {
                        std::string comboID = "##TextureCombo" + std::to_string(it->id) + "_" + std::to_string(e);
                        if (ImGui::BeginCombo(comboID.c_str(), comp.texturePath.c_str())) {
                            for (const auto& texPath : m_AvailableTextures) {
                                bool isSelected = (comp.texturePath == texPath);
                                if (ImGui::Selectable(texPath.c_str(), isSelected)) {
                                    comp.texturePath = texPath;
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::SameLine();
                        if (ImGui::Button(("Refresh##Tex" + std::to_string(it->id) + "_" + std::to_string(e)).c_str())) {
                            RefreshTextureList();
                        }

                        char texBuf[256];
                        strncpy_s(texBuf, comp.texturePath.c_str(), sizeof(texBuf));
                        texBuf[sizeof(texBuf) - 1] = '\0';
                        if (ImGui::InputText(("Manual Path / ID##TexPath" + std::to_string(it->id) + "_" + std::to_string(e)).c_str(), texBuf, sizeof(texBuf))) {
                            comp.texturePath = std::string(texBuf);
                        }

                        if (ImGui::TreeNode(("Generate Procedural Texture##ProcTex_" + std::to_string(it->id) + "_" + std::to_string(e)).c_str())) {
                            static char procName[64] = "custom_tex_1";
                            static int procType = 1;
                            static glm::vec4 color1(1.0f, 1.0f, 1.0f, 1.0f);
                            static glm::vec4 color2(0.2f, 0.2f, 0.2f, 1.0f);
                            static int cellSize = 32;

                            static std::mt19937 rng(std::random_device{}());
                            static std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
                            static std::uniform_int_distribution<int> cellDist(4, 128);
                            static std::uniform_int_distribution<int> typeDist(0, 3);

                            auto queueProceduralUpdate = [&]() {
                                if (procName[0] == '\0') return;

                                ProceduralTextureRequest req;
                                req.name = std::string(procName);
                                req.type = static_cast<ProcTexType>(procType);
                                req.color1 = color1;
                                req.color2 = color2;
                                req.cellSize = std::max(1, cellSize);
                                m_TextureRequests.push_back(req);

                                // Keep this object bound to the live procedural texture ID.
                                comp.texturePath = req.name;
                             };

                            bool changed = false;

                            changed |= ImGui::InputText("Name ID", procName, sizeof(procName));

                            const char* procTypes[] = { "Solid Color", "Checkerboard", "Gradient (Vert)", "Gradient (Horiz)" };
                            changed |= ImGui::Combo("Type", &procType, procTypes, IM_ARRAYSIZE(procTypes));

                            changed |= ImGui::ColorEdit4("Color 1", &color1.x);
                            if (procType > 0) changed |= ImGui::ColorEdit4("Color 2", &color2.x);
                            if (procType == 1) changed |= ImGui::InputInt("Cell Size", &cellSize);

                            if (ImGui::Button("Randomise##ProcTexWnd")) {
                                procType = typeDist(rng);
                                color1 = glm::vec4(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);
                                color2 = glm::vec4(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);
                                if (procType == 1) {
                                    cellSize = cellDist(rng);
                                }
                                queueProceduralUpdate();
                            }

                            // Live update as values are edited.
                            if (changed) {
                                queueProceduralUpdate();
                            }

                            ImGui::TreePop();
                        }

                        ImGui::TreePop(); 
                    }                    


                    std::string geometryNodeLabel =
                        "Current Geometry: " + comp.geometryName + "###GeometryNode_" +
                        std::to_string(it->id) + "_" + std::to_string(e);

                    if (ImGui::TreeNode(geometryNodeLabel.c_str())) {
                        static int geoTypeIdx = 0;
                        const char* geoTypes[] = { "Model File", "Cube", "Sphere", "Plane", "Cylinder", "Bowl", "Terrain", "Disk", "Grid" };
                        ImGui::Combo("Shape Type", &geoTypeIdx, geoTypes, IM_ARRAYSIZE(geoTypes));

                        static std::string selectedModel = "";
                        if (geoTypeIdx == 0) {
                            if (ImGui::BeginCombo("File", selectedModel.empty() ? "Select..." : selectedModel.c_str())) {
                                for (const auto& mod : m_AvailableModels) {
                                    if (ImGui::Selectable(mod.c_str(), selectedModel == mod)) selectedModel = mod;
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button(("Refresh##ModelsProp" + std::to_string(it->id) + "_" + std::to_string(e)).c_str())) {
                                RefreshModelList();
                            }
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

                        ImGui::TreePop();
                    }
                    ImGui::TreePop();
                }
            }

            // --- 4. Light Component ---
            if (registry.HasComponent<LightComponent>(e)) {
                bool open = ImGui::TreeNodeEx("LightComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Light")) registry.RemoveComponent<LightComponent>(e);

                if (open && registry.HasComponent<LightComponent>(e)) {
                    auto& comp = registry.GetComponent<LightComponent>(e);

                    if (registry.HasComponent<TransformComponent>(e)) {
                        auto& tr = registry.GetComponent<TransformComponent>(e);
                        if (ImGui::DragFloat3("Position", &tr.position.x, 0.1f)) {
                            tr.UpdateMatrix();
                        }
                    }

                    ImGui::ColorEdit3("Color", &comp.color.x, ImGuiColorEditFlags_Float);
                    ImGui::DragFloat("Intensity", &comp.intensity, 0.1f, 0.0f, 1000.0f);

                    ImGui::Checkbox("Enable Flicker", &comp.flickerEnabled);
                    ImGui::SliderFloat("Flicker Amount", &comp.flickerAmount, 0.0f, 1.0f, "%.2f");
                    const char* flickerPresets[] = { "None", "Fire", "Candle", "Faulty", "Pulse" };
                    ImGui::Combo("Flicker Preset", &comp.flickerPreset, flickerPresets, IM_ARRAYSIZE(flickerPresets));

                    if (ImGui::Button("Apply Fire Flicker")) {
                        comp.flickerEnabled = true;
                        comp.flickerAmount = 0.65f;
                        comp.flickerPreset = 1;
                    }

                    const char* lightTypes[] = { "Sun", "Fire", "Point", "Spotlight" };
                    ImGui::Combo("Type", &comp.type, lightTypes, IM_ARRAYSIZE(lightTypes));

                    if (comp.type == 3) {
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

                    const char* states[] = { "NORMAL", "HEATING", "BURNING", "BURNT_OUT" };
                    int stateIdx = (int)comp.state;
                    if (stateIdx >= 0 && stateIdx <= 3) ImGui::Text("State: %s", states[stateIdx]);
                    else ImGui::Text("State: %d", stateIdx);

                    ImGui::Spacing();
                    if (comp.state == ObjectState::BURNING) {
                        ImGui::TextDisabled("Active Fire Data");
                        ImGui::Separator();
                        ImGui::Text("Fire Emitter ID: %d", comp.fireEmitterId);
                        ImGui::Text("Smoke Emitter ID: %d", comp.smokeEmitterId);
                        ImGui::Text("Light Entity ID: %d", comp.fireLightEntity);

                        ImGui::Spacing();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
                        if (ImGui::Button("Extinguish Fire", ImVec2(-1, 0))) scene.StopObjectFire(e);
                        ImGui::PopStyleColor();
                    }
                    else if (comp.isFlammable) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.4f, 0.0f, 1.0f));
                        if (ImGui::Button("Ignite Object", ImVec2(-1, 0))) scene.Ignite(e);
                        ImGui::PopStyleColor();
                    }
                    ImGui::TreePop();
                }
            }

            // --- 7. Attached Emitter Component ---
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
                            float rateSlider = std::pow(em.emissionRate / 1000.0f, 1.0f / 3.0f);
                            if (ImGui::SliderFloat("Emission Rate", &rateSlider, 0.0f, 1.0f, "%.1f p/s")) {
                                em.emissionRate = std::pow(rateSlider, 3.0f) * 1000.0f;
                                scene.GetOrCreateSystem(em.props)->UpdateEmitter(em.emitterId, em.props, em.emissionRate);
                            }
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
                            newEm.props.position = pos;
                            newEm.emitterId = scene.GetOrCreateSystem(newEm.props)->AddEmitter(newEm.props, rate);
                            comp.emitters.push_back(newEm);
                        };

                        auto presets = ParticleLibrary::GetAllPresets();
                        for (const auto& [name, props] : presets) {
                            if (ImGui::MenuItem(name.c_str())) attachFunc(props, 100.0f);
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::TreePop();
                }
            }

            // --- 8. Camera Component ---
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

                    ImGui::Spacing();
                    ImGui::TextDisabled("Physics & Collision");

                    bool isNoclip = CameraSystem::IsNoclip(scene, e);
                    if (ImGui::Checkbox("Noclip Enabled", &isNoclip)) {
                        CameraSystem::SetNoclip(scene, isNoclip, e);
                    }

                    if (!isNoclip && registry.HasComponent<ColliderComponent>(e)) {
                        auto& col = registry.GetComponent<ColliderComponent>(e);
                        ImGui::DragFloat("Bulldozer Radius", &col.radius, 0.1f, 0.5f, 50.0f);
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Active Render Layers");
                    ImGui::Separator();

                    auto maskToString = [](int mask) {
                        std::string s;
                        for (int i = 0; i < SceneLayers::ActiveLayerCount; ++i) {
                            if ((mask & (1 << i)) != 0) {
                                s += "[" + SceneLayers::LayerNames[i] + "] ";
                            }
                        }
                        return s.empty() ? std::string("[None]") : s;
                        };

                    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Inside Regions: %s", maskToString(comp.insideRegionMask).c_str());
                    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Viewing Mask:   %s", maskToString(comp.viewMask).c_str());
                    ImGui::TreePop();
                }


            }

            // --- 9. Cloth Component ---
            if (registry.HasComponent<ClothComponent>(e)) {
                bool open = ImGui::TreeNodeEx("ClothComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Cloth")) registry.RemoveComponent<ClothComponent>(e);

                if (open && registry.HasComponent<ClothComponent>(e)) {
                    auto& comp = registry.GetComponent<ClothComponent>(e);

                    ImGui::Text("Grid Size: %d x %d", comp.width, comp.height);
                    ImGui::Text("Spacing: %.2f", comp.spacing);
                    ImGui::Text("Particles: %zu", comp.particles.size());

                    float currentMass = 0.5f;
                    float currentStiffness = 50.0f;
                    float currentDamping = 1.5f;

                    for (Entity p : comp.particles) {
                        if (registry.HasComponent<PhysicsComponent>(p)) {
                            auto& pc = registry.GetComponent<PhysicsComponent>(p);
                            if (!pc.isStatic) {
                                currentMass = pc.mass;
                            }
                        }
                        if (registry.HasComponent<SpringComponent>(p)) {
                            auto& sc = registry.GetComponent<SpringComponent>(p);
                            currentStiffness = sc.stiffness;
                            currentDamping = sc.damping;
                            break;
                        }
                    }

                    bool updatePhysics = false;
                    bool updateSprings = false;

                    if (ImGui::DragFloat("Particle Mass", &currentMass, 0.05f, 0.01f, 100.0f)) updatePhysics = true;
                    if (ImGui::DragFloat("Spring Stiffness", &currentStiffness, 1.0f, 0.0f, 1000.0f)) updateSprings = true;
                    if (ImGui::DragFloat("Spring Damping", &currentDamping, 0.1f, 0.0f, 100.0f)) updateSprings = true;

                    if (updatePhysics || updateSprings) {
                        for (Entity p : comp.particles) {
                            if (updatePhysics && registry.HasComponent<PhysicsComponent>(p)) {
                                auto& pc = registry.GetComponent<PhysicsComponent>(p);
                                if (!pc.isStatic) {
                                    pc.SetMass(currentMass);
                                }
                            }
                            if (updateSprings && registry.HasComponent<SpringComponent>(p)) {
                                auto& sc = registry.GetComponent<SpringComponent>(p);
                                sc.stiffness = currentStiffness;
                                sc.damping = currentDamping;
                            }
                        }
                    }

                    ImGui::Spacing();
                    if (ImGui::Checkbox("Collisions Enabled", &comp.collisionsEnabled)) {
                        for (Entity p : comp.particles) {
                            if (registry.HasComponent<ColliderComponent>(p)) {
                                registry.GetComponent<ColliderComponent>(p).hasCollision = comp.collisionsEnabled;
                            }
                        }
                    }
                    if (ImGui::Checkbox("Visualize Collision Polys", &comp.visualizeCollisionPolys)) {
                        if (registry.HasComponent<RenderComponent>(e)) {
                            auto& render = registry.GetComponent<RenderComponent>(e);
                            render.shadingMode = comp.visualizeCollisionPolys ? 4 : 1; // 4=Wireframe, 1=Phong
                        }
                    }
                    ImGui::TreePop();
                }
            }

            // --- 10. Collider Component ---
            if (registry.HasComponent<ColliderComponent>(e)) {
                bool open = ImGui::TreeNodeEx("ColliderComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Collider")) registry.RemoveComponent<ColliderComponent>(e);

                if (open && registry.HasComponent<ColliderComponent>(e)) {
                    auto& comp = registry.GetComponent<ColliderComponent>(e);
                    ImGui::Checkbox("Has Collision", &comp.hasCollision);

                    const char* shapeTypes[] = { "Sphere", "Plane", "Capsule", "Cylinder", "Cube" };
                    if (ImGui::Combo("Shape Type", &comp.type, shapeTypes, (int)IM_ARRAYSIZE(shapeTypes))) {
                        // Reset or adjust values if needed when type changes
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Sync to Visual")) {
                        if (registry.HasComponent<TransformComponent>(e)) {
                            auto& trans = registry.GetComponent<TransformComponent>(e);
                            std::string name = registry.HasComponent<NameComponent>(e) ? registry.GetComponent<NameComponent>(e).name : "";
                            
                            // Detect type from name or just use current selected type
                            if (name.find("Sphere") != std::string::npos || comp.type == 0) {
                                comp.type = 0;
                                comp.radius = std::max({trans.scale.x, trans.scale.y, trans.scale.z}) * 0.5f;
                            }
                            else if (name.find("Plane") != std::string::npos || comp.type == 1) {
                                comp.type = 1;
                                comp.normal = glm::vec3(0, 1, 0); // Default up
                            }
                            else if (name.find("Cube") != std::string::npos || comp.type == 4) {
                                comp.type = 4;
                                // Cube uses radius for extent/size in some parts of our physics
                                comp.radius = std::max({trans.scale.x, trans.scale.y, trans.scale.z}) * 0.5f;
                            }
                            else if (name.find("Cylinder") != std::string::npos || name.find("Smoke") != std::string::npos || comp.type == 3) {
                                comp.type = 3;
                                comp.radius = std::max(trans.scale.x, trans.scale.z) * 0.5f;
                                comp.height = trans.scale.y;
                            }
                            else if (name.find("Capsule") != std::string::npos || comp.type == 2) {
                                comp.type = 2;
                                comp.radius = std::max(trans.scale.x, trans.scale.z) * 0.5f;
                                comp.height = trans.scale.y;
                            }
                        }
                    }

                    if (comp.type == 0) { // Sphere
                        ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.0f, 100.0f);
                    }
                    else if (comp.type == 1) { // Plane
                        if (ImGui::DragFloat3("Normal", &comp.normal.x, 0.05f)) {
                            if (glm::length(comp.normal) > 0.001f) comp.normal = glm::normalize(comp.normal);
                        }
                    }
                    else if (comp.type == 2 || comp.type == 3) { // Capsule / Cylinder
                        ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.0f, 100.0f);
                        ImGui::DragFloat("Height", &comp.height, 0.1f, 0.0f, 100.0f);
                    }
                    else if (comp.type == 4) { // Cube
                        ImGui::DragFloat("Half Extent/Size", &comp.radius, 0.1f, 0.0f, 100.0f);
                    }
                    ImGui::TreePop();
                }
            }

            // --- 10. Physics Component ---
            if (registry.HasComponent<PhysicsComponent>(e)) {
                bool open = ImGui::TreeNodeEx("PhysicsComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Physics")) registry.RemoveComponent<PhysicsComponent>(e);

                if (open && registry.HasComponent<PhysicsComponent>(e)) {
                    auto& comp = registry.GetComponent<PhysicsComponent>(e);

                    ImGui::Checkbox("Is Static", &comp.isStatic);

                    float tempMass = comp.mass;
                    if (ImGui::DragFloat("Mass", &tempMass, 0.1f, 0.0f, 1000.0f)) {
                        comp.SetMass(tempMass);
                    }

                    ImGui::TextDisabled("Inverse Mass: %.4f", comp.inverseMass);

                    ImGui::DragFloat("Restitution (Bounciness)", &comp.restitution, 0.05f, 0.0f, 2.0f);
                    ImGui::DragFloat("Friction", &comp.friction, 0.01f, 0.0f, 1.0f);

                    ImGui::Spacing();
                    ImGui::DragFloat3("Velocity", &comp.velocity.x, 0.1f);
                    ImGui::DragFloat3("Force Accumulator", &comp.forceAccumulator.x, 0.1f);

                    ImGui::TreePop();
                }
            }

            // --- 11. Spring Component ---
            if (registry.HasComponent<SpringComponent>(e)) {
                bool open = ImGui::TreeNodeEx("SpringComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Spring")) registry.RemoveComponent<SpringComponent>(e);

                if (open && registry.HasComponent<SpringComponent>(e)) {
                    auto& spring = registry.GetComponent<SpringComponent>(e);

                    ImGui::Checkbox("Dynamic Entity Anchor", &spring.isAttachedToEntity);

                    if (!spring.isAttachedToEntity) {
                        ImGui::DragFloat3("World Anchor Point", &spring.fixedAnchorPoint.x, 0.1f);
                        // Set anchor to this entity's transform if available
                        if (registry.HasComponent<TransformComponent>(e)) {
                            if (ImGui::Button("Set Anchor to Current Transform")) {
                                spring.fixedAnchorPoint = registry.GetComponent<TransformComponent>(e).position;
                            }
                        }
                    }
                    else {
                        ImGui::TextDisabled("Anchor: This Entity's Transform (Real-time)");
                        if (registry.HasComponent<TransformComponent>(e)) {
                            const glm::vec3 pos = registry.GetComponent<TransformComponent>(e).position;
                            ImGui::Text("Current Pos: [%.2f, %.2f, %.2f]", pos.x, pos.y, pos.z);
                        }
                        else {
                            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Warning: Missing TransformComponent");
                        }

                        ImGui::Text("Connected Entities:");
                        ImGui::Separator();

                        // List current connections with remove buttons
                        for (size_t i = 0; i < spring.connectedEntities.size(); ++i) {
                            Entity id = spring.connectedEntities[i];
                            std::string label = "ID: " + std::to_string(id);
                            if (id < registry.GetEntityCount() && registry.HasComponent<NameComponent>(id)) {
                                label += " (" + registry.GetComponent<NameComponent>(id).name + ")";
                            }
                            ImGui::Text("%s", label.c_str());
                            ImGui::SameLine();
                            ImGui::PushID(static_cast<int>(i));
                            if (ImGui::Button("Remove")) {
                                spring.connectedEntities.erase(spring.connectedEntities.begin() + i);
                                ImGui::PopID();
                                break;
                            }
                            ImGui::PopID();
                        }

                        ImGui::Separator();

                        // Add by ID input
                        static int newEntityId = -1;
                        ImGui::InputInt("Entity ID to Add", &newEntityId);
                        ImGui::SameLine();
                        if (ImGui::Button("Add")) {
                            if (newEntityId >= 0 && static_cast<Entity>(newEntityId) != e && newEntityId < static_cast<int>(registry.GetEntityCount())) {
                                if (std::find(spring.connectedEntities.begin(), spring.connectedEntities.end(), static_cast<Entity>(newEntityId)) == spring.connectedEntities.end()) {
                                    spring.connectedEntities.push_back(static_cast<Entity>(newEntityId));
                                }
                            }
                        }

                        ImGui::Separator();

                        // Add from scene list for convenience (entities that have transforms)
                        ImGui::TextDisabled("Attach from Scene:");
                        ImGui::BeginChild("SpringAttachList", ImVec2(0, 120), true);
                        for (Entity cand = 0; cand < registry.GetEntityCount(); ++cand) {
                            if (cand == e) continue; // don't attach self
                            if (!registry.HasComponent<TransformComponent>(cand)) continue;

                            std::string candLabel = "ID: " + std::to_string(cand);
                            if (registry.HasComponent<NameComponent>(cand)) {
                                candLabel += " (" + registry.GetComponent<NameComponent>(cand).name + ")";
                            }

                            ImGui::PushID(static_cast<int>(cand));
                            ImGui::Text("%s", candLabel.c_str());
                            ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
                            if (ImGui::Button("Add")) {
                                if (std::find(spring.connectedEntities.begin(), spring.connectedEntities.end(), cand) == spring.connectedEntities.end()) {
                                    spring.connectedEntities.push_back(cand);
                                }
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }

                    ImGui::DragFloat("Resting Length", &spring.restingLength, 0.1f, 0.0f, 100.0f);
                    ImGui::DragFloat("Stiffness (k)", &spring.stiffness, 0.1f, 0.0f, 1000.0f);
                    ImGui::DragFloat("Damping (b)", &spring.damping, 0.01f, 0.0f, 100.0f);

                    ImGui::TreePop();
                }
            }

            // --- Path Animation Component ---
            if (registry.HasComponent<PathAnimationComponent>(e)) {
                bool open = ImGui::TreeNodeEx("PathAnimationComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##PathAnimation")) registry.RemoveComponent<PathAnimationComponent>(e);

                if (open && registry.HasComponent<PathAnimationComponent>(e)) {
                    auto& comp = registry.GetComponent<PathAnimationComponent>(e);
                    bool dirty = false;

                    auto syncPathTopology = [&]() {
                        const size_t targetSegmentCount = comp.connectEndToStart && comp.waypoints.size() > 1
                            ? comp.waypoints.size()
                            : (comp.waypoints.size() > 0 ? comp.waypoints.size() - 1 : 0);

                        bool changed = false;
                        while (comp.segments.size() < targetSegmentCount) {
                            PathCurveSegment segment;
                            const size_t segmentIndex = comp.segments.size();
                            const size_t startIndex = segmentIndex;
                            const size_t endIndex = (comp.connectEndToStart && segmentIndex + 1 == comp.waypoints.size()) ? 0 : segmentIndex + 1;

                            if (startIndex < comp.waypoints.size() && endIndex < comp.waypoints.size()) {
                                const glm::vec3 start = comp.waypoints[startIndex].position;
                                const glm::vec3 end = comp.waypoints[endIndex].position;
                                segment.controlPoint = (start + end) * 0.5f;
                            }

                            comp.segments.push_back(segment);
                            changed = true;
                        }

                        if (comp.segments.size() > targetSegmentCount) {
                            comp.segments.resize(targetSegmentCount);
                            changed = true;
                        }

                        return changed;
                    };

                    int playMode = static_cast<int>(comp.playMode);
                    const char* playModeItems[] = { "Once", "Loop", "Bounce" };
                    if (ImGui::Combo("Play Mode", &playMode, playModeItems, IM_ARRAYSIZE(playModeItems))) {
                        comp.playMode = static_cast<PathAnimationPlayMode>(playMode);
                        dirty = true;
                    }

                    int easing = static_cast<int>(comp.easing);
                    const char* easingItems[] = { "Linear", "Smoothstep" };
                    if (ImGui::Combo("Easing", &easing, easingItems, IM_ARRAYSIZE(easingItems))) {
                        comp.easing = static_cast<PathAnimationEasing>(easing);
                        dirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(comp.applyEasing ? "Disable Easing##PathAnimationEasingToggle" : "Apply Easing##PathAnimationEasingToggle")) {
                        comp.applyEasing = !comp.applyEasing;
                        dirty = true;
                    }

                    int timingMode = static_cast<int>(comp.timingMode);
                    const char* timingItems[] = { "Absolute Times", "Per Segment", "Overall Time" };
                    if (ImGui::Combo("Timing Mode", &timingMode, timingItems, IM_ARRAYSIZE(timingItems))) {
                        comp.timingMode = static_cast<PathAnimationTimingMode>(timingMode);
                        dirty = true;
                    }

                    if (ImGui::DragFloat("Total Duration", &comp.totalDuration, 0.05f, 0.0f, 10000.0f)) {
                        comp.totalDuration = std::max(0.0f, comp.totalDuration);
                        dirty = true;
                    }

                    if (comp.isPlaying) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.28f, 0.28f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.14f, 0.14f, 1.0f));
                        if (ImGui::Button("Stop##PathAnimationPlayToggle")) {
                            comp.isPlaying = false;
                        }
                        ImGui::PopStyleColor(3);
                    }
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.70f, 0.25f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.80f, 0.33f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.14f, 0.56f, 0.20f, 1.0f));
                        if (ImGui::Button("Play##PathAnimationPlayToggle")) {
                            comp.isPlaying = true;
                        }
                        ImGui::PopStyleColor(3);
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Restart##PathAnimationRestart")) {
                        comp.currentTime = 0.0f;
                        comp.rotationSpinTime = 0.0f;
                        comp.isPlaying = false;
                    }

                    ImGui::Checkbox("Show Path", &comp.showPath);
                    ImGui::Checkbox("Use Relative Positioning", &comp.useLocalSpace);
                    if (ImGui::Checkbox("Connect End to Start", &comp.connectEndToStart)) {
                        dirty = true;
                    }
                    if (ImGui::Checkbox("Reverse Path", &comp.reversePath)) {
                        dirty = true;
                    }
                    if (ImGui::Checkbox("Per Point Rotation", &comp.perPointRotation)) {
                        dirty = true;
                    }
                    if (ImGui::Checkbox("Apply Constant Rotation", &comp.applyConstantRotation)) {
                        dirty = true;
                    }
                    if (comp.applyConstantRotation) {
                        if (ImGui::DragFloat3("Spin Rate (deg/s)", &comp.rotationSpinRate.x, 0.1f)) {
                            dirty = true;
                        }
                    }
                    if (ImGui::DragFloat("Playback Speed", &comp.playbackSpeed, 0.05f, 0.0f, 10.0f, "%.2fx")) {
                        comp.playbackSpeed = std::max(0.0f, comp.playbackSpeed);
                    }
                    ImGui::TextDisabled("Runtime Velocity: %.2f %.2f %.2f", comp.animationVelocity.x, comp.animationVelocity.y, comp.animationVelocity.z);
                    if (ImGui::ColorEdit4("Path Color", &comp.pathColor.x, ImGuiColorEditFlags_AlphaPreview)) {
                        dirty = true;
                    }

                    if (syncPathTopology()) {
                        dirty = true;
                    }

                    ImGui::Separator();
                    for (size_t i = 0; i < comp.waypoints.size(); ++i) {
                        auto& waypoint = comp.waypoints[i];
                        const bool isLoopSegment = comp.connectEndToStart && comp.waypoints.size() > 1 && i == comp.waypoints.size() - 1;
                        const bool hasOutgoingSegment = i < comp.segments.size() && (i + 1 < comp.waypoints.size() || isLoopSegment);
                        const size_t endIndex = isLoopSegment ? 0 : (i + 1);

                        std::string nodeLabel = hasOutgoingSegment
                            ? ("Waypoint " + std::to_string(i) + " (-> " + std::to_string(endIndex) + ")")
                            : ("Waypoint " + std::to_string(i));

                        if (ImGui::TreeNode(nodeLabel.c_str())) {
                            if (ImGui::DragFloat3(("Position##PathWaypointPos" + std::to_string(i)).c_str(), &waypoint.position.x, 0.05f)) dirty = true;
                            if (!comp.rotateAlongPath && comp.perPointRotation) {
                                if (ImGui::DragFloat3(("Orientation##PathWaypointRot" + std::to_string(i)).c_str(), &waypoint.orientation.x, 0.1f)) dirty = true;
                            }
                            if (comp.timingMode == PathAnimationTimingMode::Absolute &&
                                ImGui::DragFloat(("Time From Start##PathWaypointTime" + std::to_string(i)).c_str(), &waypoint.timeFromStart, 0.05f, 0.0f, 10000.0f)) {
                                waypoint.timeFromStart = std::max(0.0f, waypoint.timeFromStart);
                                dirty = true;
                            }

                            if (hasOutgoingSegment) {
                                auto& segment = comp.segments[i];
                                ImGui::Separator();
                                ImGui::TextDisabled("Outgoing Segment");

                                int curveType = static_cast<int>(segment.curveType);
                                const char* curveItems[] = { "Straight", "Bezier Quadratic" };
                                if (ImGui::Combo(("Curve Type##PathSegmentCurve" + std::to_string(i)).c_str(), &curveType, curveItems, IM_ARRAYSIZE(curveItems))) {
                                    segment.curveType = static_cast<PathCurveType>(curveType);
                                    dirty = true;
                                }

                                if (segment.curveType == PathCurveType::BezierQuadratic) {
                                    if (ImGui::DragFloat3(("Control##PathSegmentCtrl" + std::to_string(i)).c_str(), &segment.controlPoint.x, 0.05f)) {
                                        dirty = true;
                                    }
                                }

                                if (comp.timingMode == PathAnimationTimingMode::PerSegment) {
                                    if (ImGui::DragFloat(("Duration##PathSegmentDuration" + std::to_string(i)).c_str(), &segment.duration, 0.05f, 0.001f, 10000.0f)) {
                                        segment.duration = std::max(0.001f, segment.duration);
                                        dirty = true;
                                    }
                                }
                            }

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
                            if (ImGui::Button(("Remove Waypoint##PathWaypointRemove" + std::to_string(i)).c_str())) {
                                comp.waypoints.erase(comp.waypoints.begin() + static_cast<long long>(i));
                                dirty = true;
                                ImGui::PopStyleColor();
                                ImGui::TreePop();
                                break;
                            }
                            ImGui::PopStyleColor();

                            ImGui::TreePop();
                        }
                    }

                    if (ImGui::Button("Add Waypoint##PathWaypointAdd")) {
                        PathWaypoint waypoint;
                        PathCurveSegment segment;
                        if (!comp.waypoints.empty()) {
                            const auto& last = comp.waypoints.back();
                            waypoint.position = last.position + glm::vec3(1.0f, 0.0f, 0.0f);
                            waypoint.orientation = comp.rotateAlongPath ? glm::vec3(0.0f) : last.orientation;
                            waypoint.timeFromStart = last.timeFromStart + 1.0f;
                            segment.controlPoint = (last.position + waypoint.position) * 0.5f;
                            comp.segments.push_back(segment);
                        }
                        comp.waypoints.push_back(waypoint);
                        dirty = true;
                    }

                    if (syncPathTopology()) {
                        dirty = true;
                    }

                    if (dirty) {
                        comp.initialized = false;
                        comp.hasLastEvaluatedPosition = false;
                        comp.hasLocalOrigin = false;
                        comp.hasBaseRotation = false;
                    }

                    ImGui::TreePop();
                }
            }

            // --- 12. Environment Component ---
            if (registry.HasComponent<EnvironmentComponent>(e)) {
                bool open = ImGui::TreeNodeEx("EnvironmentComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Environment")) {
                    registry.RemoveComponent<EnvironmentComponent>(e);
                    scene.InvalidateEnvironmentEntity(e);
                }

                if (open && registry.HasComponent<EnvironmentComponent>(e)) {
                    auto& comp = registry.GetComponent<EnvironmentComponent>(e);
                    ImGui::Checkbox("Use Simple Shadows", &comp.useSimpleShadows);
                    ImGui::Checkbox("Is Precipitating", &comp.isPrecipitating);
                    ImGui::DragFloat("Sun Heat Bonus", &comp.sunHeatBonus, 0.1f);
                    ImGui::TreePop();
                }
            }

            // --- 12. Layer Region Component (Layer Entity) ---
            if (registry.HasComponent<LayerRegionComponent>(e)) {
                bool open = ImGui::TreeNodeEx("Layer Configuration", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##LayerRegion")) registry.RemoveComponent<LayerRegionComponent>(e);

                if (open && registry.HasComponent<LayerRegionComponent>(e)) {
                    auto& comp = registry.GetComponent<LayerRegionComponent>(e);

                    // 1. Layer Identity
                    char nameBuf[64];
                    strncpy_s(nameBuf, comp.layerName.c_str(), sizeof(nameBuf));
                    nameBuf[sizeof(nameBuf) - 1] = '\0';
                    if (ImGui::InputText("Layer Name", nameBuf, sizeof(nameBuf))) {
                        comp.layerName = std::string(nameBuf);
                        // Sync this entity's name to the global engine layer names!
                        if (comp.assignedLayerBit >= 1 && comp.assignedLayerBit < SceneLayers::MAX_LAYERS) {
                            SceneLayers::LayerNames[comp.assignedLayerBit] = comp.layerName;
                        }
                    }

                    // Let the user pick which Layer Slot (1-7) this entity represents
                    const int previousBit = comp.assignedLayerBit;
                    if (ImGui::SliderInt("Layer Slot", &comp.assignedLayerBit, 1, SceneLayers::MAX_LAYERS - 1)) {
                        comp.assignedLayerBit = std::clamp(comp.assignedLayerBit, 1, SceneLayers::MAX_LAYERS - 1);

                        // If this layer name was mapped to the old bit, clear old mapping label
                        if (previousBit >= 1 &&
                            previousBit < SceneLayers::MAX_LAYERS &&
                            SceneLayers::LayerNames[previousBit] == comp.layerName) {
                            SceneLayers::LayerNames[previousBit] = std::string("Layer ") + static_cast<char>('A' + previousBit);
                        }

                        // Always map this region's name to its new bit
                        SceneLayers::LayerNames[comp.assignedLayerBit] = comp.layerName;
                    }

                    ImGui::TextDisabled(
                        "Slot = Layer %c | Bit = %d | Mask = %d",
                        static_cast<char>('A' + comp.assignedLayerBit),
                        comp.assignedLayerBit,
                        (1 << comp.assignedLayerBit)
                    );

                    // 2. Spatial Bounds
                    ImGui::Spacing();
                    ImGui::TextDisabled("Layer Boundaries");
                    const char* volumeTypes[] = { "Sphere", "Box (AABB)" };
                    ImGui::Combo("Volume Type", &comp.volumeType, volumeTypes, IM_ARRAYSIZE(volumeTypes));

                    if (comp.volumeType == 0) {
                        ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.1f, 1000.0f);
                    }
                    else {
                        ImGui::DragFloat3("Half Extents", &comp.halfExtents.x, 0.1f, 0.1f, 1000.0f);
                    }

                    ImGui::Spacing();
                    ImGui::TextDisabled("Region Debug");
                    ImGui::Checkbox("Show Region", &comp.showRegionDebug);
                    ImGui::ColorEdit4("Region Color", &comp.regionDebugColor.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float);
                    ImGui::SameLine();
                    if (ImGui::Button("Randomise##RegionColorWnd")) {
                        static std::mt19937 rng(std::random_device{}());
                        static std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
                        comp.regionDebugColor.r = colorDist(rng);
                        comp.regionDebugColor.g = colorDist(rng);
                        comp.regionDebugColor.b = colorDist(rng);
                        comp.regionDebugColor.a = 0.25f;
                    }

                    bool regionsOnly = scene.GetRegionsOnlyDebugView();
                    if (ImGui::Checkbox("Regions Only (Global)", &regionsOnly)) {
                        scene.SetRegionsOnlyDebugView(regionsOnly);
                    }

                    // 3. Live List of Assigned Objects!
                    ImGui::Spacing();
                    ImGui::TextDisabled("Objects Assigned to this Layer");
                    ImGui::Separator();

                    ImGui::BeginChild("LayerObjectsList", ImVec2(0, 170), true);
                    int listedCount = 0;

                    static const char* modeItems[] = {
                        "Disabled",
                        "Enabled",
                        "Only In Region"
                    };

                    const int bitMask = (1 << comp.assignedLayerBit);

                    for (Entity other = 0; other < registry.GetEntityCount(); ++other) {
                        if (!registry.HasComponent<RenderComponent>(other)) continue;

                        auto& otherRender = registry.GetComponent<RenderComponent>(other);

                        int mode = 0; // 0=Disabled, 1=Enabled, 2=OnlyInRegion
                        if ((otherRender.onlyInRegionMask & bitMask) != 0) mode = 2;
                        else if ((otherRender.layerMask & bitMask) != 0) mode = 1;

                        std::string objName = "Entity " + std::to_string(other);
                        if (registry.HasComponent<NameComponent>(other)) {
                            objName = registry.GetComponent<NameComponent>(other).name;
                        }

                        ImGui::PushID(static_cast<int>(other));

                        ImGui::Text(" %s", objName.c_str());
                        ImGui::SameLine(ImGui::GetWindowWidth() - 185.0f);

                        int newMode = mode;
                        if (ImGui::Combo("##LayerModeInline", &newMode, modeItems, IM_ARRAYSIZE(modeItems))) {
                            // Clear this layer bit first
                            otherRender.layerMask &= ~bitMask;
                            otherRender.onlyInRegionMask &= ~bitMask;

                            // Apply selected mode
                            if (newMode == 1) { // Enabled
                                otherRender.layerMask |= bitMask;
                            }
                            else if (newMode == 2) { // Only In Region
                                otherRender.layerMask |= bitMask;
                                otherRender.onlyInRegionMask |= bitMask;
                            }
                        }

                        ImGui::PopID();
                        listedCount++;
                    }

                    if (listedCount == 0) {
                        ImGui::TextDisabled(" No renderable objects found.");
                    }
                    ImGui::EndChild();

                    ImGui::TreePop();
                }
            }

            ImGui::Spacing();

            // --- 13. Despawner Component ---
            if (registry.HasComponent<DespawnerComponent>(e)) {
                bool open = ImGui::TreeNodeEx("DespawnerComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Despawner")) registry.RemoveComponent<DespawnerComponent>(e);

                if (open && registry.HasComponent<DespawnerComponent>(e)) {
                    auto& comp = registry.GetComponent<DespawnerComponent>(e);
                    ImGui::Checkbox("Enabled", &comp.enabled);
                    ImGui::TreePop();
                }
            }

            // --- 14. Smoke Grenade Component ---
            if (registry.HasComponent<SmokeGrenadeComponent>(e)) {
                bool open = ImGui::TreeNodeEx("SmokeGrenadeComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##SmokeGrenade")) registry.RemoveComponent<SmokeGrenadeComponent>(e);

                if (open && registry.HasComponent<SmokeGrenadeComponent>(e)) {
                    auto& comp = registry.GetComponent<SmokeGrenadeComponent>(e);
                    ImGui::DragFloat("Timer", &comp.timer, 0.01f);
                    ImGui::DragFloat("Delay Before Smoke", &comp.delayBeforeSmoke, 0.1f);
                    ImGui::DragFloat("Smoke Duration", &comp.smokeDuration, 0.1f);
                    ImGui::Checkbox("Is Emitting", &comp.isEmitting);
                    ImGui::Text("Smoke Emitter ID: %d", comp.smokeEmitterId);
                    ImGui::TreePop();
                }
            }
            
            // --- 15. Object Spawner Component ---
            if (registry.HasComponent<ObjectSpawnerComponent>(e)) {
                bool open = ImGui::TreeNodeEx("ObjectSpawnerComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Spawner")) registry.RemoveComponent<ObjectSpawnerComponent>(e);

                if (open && registry.HasComponent<ObjectSpawnerComponent>(e)) {
                    auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
                    
                    ImGui::Checkbox("Always On", &spawner.alwaysOn);
                    ImGui::Checkbox("Is Running", &spawner.isRunning);
                    ImGui::Checkbox("Trigger On Startup", &spawner.triggerOnStartup);
                    
                    char groupBuf[2] = { spawner.group, '\0' };
                    if (ImGui::InputText("Group (A-D)", groupBuf, 2)) {
                        char g = static_cast<char>(std::toupper(static_cast<unsigned char>(groupBuf[0])));
                        if (g >= 'A' && g <= 'D') spawner.group = g;
                    }

                    ImGui::DragFloat("Spawn Interval", &spawner.spawnInterval, 0.05f, 0.01f, 60.0f);
                    
                    ImGui::BeginDisabled(spawner.alwaysOn);
                    ImGui::DragFloat("Run Duration", &spawner.runDurationSeconds, 0.1f, -1.0f, 3600.0f);
                    ImGui::InputInt("Max Spawns Per Run", &spawner.maxSpawnsPerRun);
                    ImGui::EndDisabled();

                    const char* geoTypes[] = { "Sphere", "Cube", "Plane", "Model", "Smoke Grenade" };
                    int geoIdx = 0;
                    if (spawner.spawnGeometryType == "Cube") geoIdx = 1;
                    else if (spawner.spawnGeometryType == "Plane") geoIdx = 2;
                    else if (spawner.spawnGeometryType == "Model") geoIdx = 3;
                    else if (spawner.spawnGeometryType == "Smoke Grenade") geoIdx = 4;
                    
                    if (ImGui::Combo("Spawn Geometry", &geoIdx, geoTypes, (int)IM_ARRAYSIZE(geoTypes))) {
                        spawner.spawnGeometryType = geoTypes[geoIdx];
                    }

                    if (spawner.spawnGeometryType == "Model") {
                        char modelBuf[256];
                        strncpy_s(modelBuf, spawner.spawnModelPath.c_str(), sizeof(modelBuf));
                        modelBuf[sizeof(modelBuf) - 1] = '\0';
                        if (ImGui::InputText("Model Path", modelBuf, sizeof(modelBuf))) {
                            spawner.spawnModelPath = std::string(modelBuf);
                        }
                    }

                    ImGui::DragFloat3("Spawn Scale", &spawner.spawnScale.x, 0.05f, 0.01f, 100.0f);
                    ImGui::DragFloat3("Spawn Velocity", &spawner.spawnVelocity.x, 0.1f);
                    ImGui::Checkbox("Randomize Velocity", &spawner.randomizeVelocity);
                    if (spawner.randomizeVelocity) {
                        ImGui::DragFloat3("Velocity Range", &spawner.randomVelocityRange.x, 0.1f);
                    }

                    ImGui::DragFloat3("Spawn Spin", &spawner.spawnAngularVelocity.x, 0.05f);
                    ImGui::Checkbox("Randomize Spin", &spawner.randomizeAngularVelocity);
                    if (spawner.randomizeAngularVelocity) {
                        ImGui::DragFloat3("Spin Range", &spawner.randomAngularVelocityRange.x, 0.05f);
                    }

                    ImGui::DragFloat("Spawn Mass", &spawner.spawnMass, 0.1f, 0.01f, 1000.0f);
                    ImGui::DragFloat("Spawn Lifespan", &spawner.spawnLifespanSeconds, 0.1f, -1.0f, 600.0f);

                    ImGui::Separator();
                    ImGui::TextDisabled("Entity Attachment");
                    ImGui::Checkbox("Attach to Target##Prop", &spawner.attachToTarget);
                    if (spawner.attachToTarget) {
                        char targetBuf[64];
                        strncpy_s(targetBuf, spawner.attachTargetName.c_str(), sizeof(targetBuf));
                        targetBuf[sizeof(targetBuf) - 1] = '\0';
                        if (ImGui::InputText("Target Name##Prop", targetBuf, sizeof(targetBuf))) {
                            spawner.attachTargetName = std::string(targetBuf);
                        }
                        ImGui::TextDisabled("Empty = Active Camera");
                    }

                    ImGui::Separator();
                    ImGui::Text("Spawned Count: %d", spawner.spawnedCount);
                    if (ImGui::Button("Fire Once")) {
                        ObjectSpawnerSystem::FireOnce(scene, e);
                    }
                    
                    ImGui::TreePop();
                }
            }

            // --- Component Assignment Menu ---
            if (ImGui::BeginMenu("Add Component...")) {
                addMenuItem((NameComponent*)nullptr, "NameComponent", e);
                addMenuItem((TransformComponent*)nullptr, "TransformComponent", e);
                addMenuItem((RenderComponent*)nullptr, "RenderComponent", e);
                addMenuItem((OrbitComponent*)nullptr, "OrbitComponent", e);
                addMenuItem((ThermoComponent*)nullptr, "ThermoComponent", e);
                addMenuItem((ColliderComponent*)nullptr, "ColliderComponent", e);
                addMenuItem((PhysicsComponent*)nullptr, "PhysicsComponent", e);
                addMenuItem((SpringComponent*)nullptr, "SpringComponent", e);
                addMenuItem((PathAnimationComponent*)nullptr, "PathAnimationComponent", e);
                addMenuItem((LightComponent*)nullptr, "LightComponent", e);
                addMenuItem((CameraComponent*)nullptr, "CameraComponent", e);
                addMenuItem((AttachedEmitterComponent*)nullptr, "AttachedEmitterComponent", e);
                addMenuItem((EnvironmentComponent*)nullptr, "EnvironmentComponent", e);
                addMenuItem((DustCloudComponent*)nullptr, "DustCloudComponent", e);
                addMenuItem((DespawnerComponent*)nullptr, "DespawnerComponent", e);
                addMenuItem((LayerRegionComponent*)nullptr, "LayerRegionComponent", e);
                addMenuItem((SmokeGrenadeComponent*)nullptr, "SmokeGrenadeComponent", e);
                addMenuItem((ObjectSpawnerComponent*)nullptr, "ObjectSpawnerComponent", e);
                ImGui::EndMenu();
            }

        }
        else {
            ImGui::TextDisabled("Select an entity from the list to view and edit its properties.");
        }

        ImGui::EndChild(); // End Details Pane
    }
    ImGui::End();

    ++it;
} // End of m_PropertyWindows loop

// ADD THIS AT THE VERY END OF THE FUNCTION
for (Entity popEntity : popoutRequests) {
    // Add new window: { id, selectedEntity, showList, isOpen, isPopout }
    m_PropertyWindows.push_back({ m_NextPropertyWindowId++, popEntity, false, true, true });
}
}
