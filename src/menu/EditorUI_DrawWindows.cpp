#include "EditorUI.h"
#include "imgui.h"
#include "../rendering/ParticleLibrary.h"
#include "../systems/CameraSystem.h"
#include <algorithm>

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

}

void EditorUI::DrawPropertyWindowsSection(Scene& scene, Entity& entityToDelete) {
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
        visibleBits.reserve(SceneLayers::MAX_LAYERS);

        for (int i = 0; i < SceneLayers::MAX_LAYERS; ++i) {
            const int bitMask = (1 << i);
            const bool currentlySet =
                (visibleMask & bitMask) != 0 ||
                (onlyMask & bitMask) != 0;

            if (isLayerUsed(i) || currentlySet) {
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
    std::string windowTitle = "Entity Properties (Window " + std::to_string(it->id) + ")###PropWin" + std::to_string(it->id);

    if (ImGui::Begin(windowTitle.c_str(), &it->isOpen)) {
        Registry& registry = scene.GetRegistry();
        Entity count = registry.GetEntityCount();

        // Top bar for the window (Retract button)
        if (ImGui::Button(it->showList ? "<< Hide Entity List" : ">> Show Entity List")) {
            it->showList = !it->showList;
        }
        ImGui::Separator();
        ImGui::Spacing();

        // --- LEFT PANE: Selectable Entity List (Retractable) ---
        if (it->showList) {
            ImGui::BeginChild("EntityListPane", ImVec2(250, 0), true);

            for (Entity e = 0; e < count; ++e) {
                std::string entityName = "Entity " + std::to_string(e);
                if (registry.HasComponent<NameComponent>(e)) {
                    entityName = registry.GetComponent<NameComponent>(e).name + " (ID: " + std::to_string(e) + ")";
                }

                bool isSelected = (it->selectedEntity == e);
                if (ImGui::Selectable(entityName.c_str(), isSelected)) {
                    it->selectedEntity = e;
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

            ImGui::TextDisabled("%s", headerName.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("Delete Entity", ImVec2(-1, 0))) {
                entityToDelete = e;
            }
            ImGui::PopStyleColor(3);

            const bool canFocusObject = registry.HasComponent<TransformComponent>(e);
            if (!canFocusObject) ImGui::BeginDisabled();
            if (ImGui::Button("Focus Camera on Object", ImVec2(-1, 0)) && canFocusObject) {
                m_ViewRequested = e;
            }
            if (!canFocusObject) ImGui::EndDisabled();

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

                    ImGui::Text("Texture:");
                    std::string comboID = "##TextureCombo" + std::to_string(it->id);
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
                    if (ImGui::Button(("Refresh##Tex" + std::to_string(it->id)).c_str())) RefreshTextureList();

                    char texBuf[256];
                    strncpy_s(texBuf, comp.texturePath.c_str(), sizeof(texBuf));
                    texBuf[sizeof(texBuf) - 1] = '\0';
                    if (ImGui::InputText("Manual Path / ID", texBuf, sizeof(texBuf))) {
                        comp.texturePath = std::string(texBuf);
                    }

                    // Procedural Texture Generator
                    if (ImGui::TreeNode("Generate Procedural Texture")) {
                        static char procName[64] = "custom_tex_1";
                        static int procType = 1;
                        static glm::vec4 color1(1.0f, 1.0f, 1.0f, 1.0f);
                        static glm::vec4 color2(0.2f, 0.2f, 0.2f, 1.0f);
                        static int cellSize = 32;

                        ImGui::InputText("Name ID", procName, sizeof(procName));

                        const char* procTypes[] = { "Solid Color", "Checkerboard", "Gradient (Vert)", "Gradient (Horiz)" };
                        ImGui::Combo("Type", &procType, procTypes, IM_ARRAYSIZE(procTypes));

                        ImGui::ColorEdit4("Color 1", &color1.x);
                        if (procType > 0) ImGui::ColorEdit4("Color 2", &color2.x);
                        if (procType == 1) ImGui::InputInt("Cell Size", &cellSize);

                        if (ImGui::Button("Generate & Apply", ImVec2(-1, 0))) {
                            ProceduralTextureRequest req;
                            req.name = std::string(procName);
                            req.type = static_cast<ProcTexType>(procType);
                            req.color1 = color1;
                            req.color2 = color2;
                            req.cellSize = cellSize;
                            m_TextureRequests.push_back(req);
                            comp.texturePath = req.name;
                        }
                        ImGui::TreePop();
                    }

                    // Change Geometry
                    if (ImGui::TreeNode("Change Geometry")) {
                        ImGui::TextWrapped("Current Geometry: %s", comp.geometryName.c_str());
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        static int geoTypeIdx = 0;
                        const char* geoTypes[] = { "Model File", "Cube", "Sphere", "Bowl", "Terrain" };
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
                            if (ImGui::Button(("Refresh##ModelsProp" + std::to_string(it->id)).c_str())) RefreshModelList();
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
                    ImGui::ColorEdit3("Color", &comp.color.x, ImGuiColorEditFlags_Float);
                    ImGui::DragFloat("Intensity", &comp.intensity, 0.1f, 0.0f, 1000.0f);

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

                    ImGui::TreePop();

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

            // --- 9. Collider Component ---
            if (registry.HasComponent<ColliderComponent>(e)) {
                bool open = ImGui::TreeNodeEx("ColliderComponent", ImGuiTreeNodeFlags_DefaultOpen);
                ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
                if (ImGui::Button("Remove##Collider")) registry.RemoveComponent<ColliderComponent>(e);

                if (open && registry.HasComponent<ColliderComponent>(e)) {
                    auto& comp = registry.GetComponent<ColliderComponent>(e);
                    ImGui::Checkbox("Has Collision", &comp.hasCollision);

                    const char* shapeTypes[] = { "Sphere", "Plane" };
                    ImGui::Combo("Shape Type", &comp.type, shapeTypes, IM_ARRAYSIZE(shapeTypes));

                    if (comp.type == 0) {
                        ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.0f, 100.0f);
                    }
                    else if (comp.type == 1) {
                        if (ImGui::DragFloat3("Normal", &comp.normal.x, 0.05f)) {
                            if (glm::length(comp.normal) > 0.001f) comp.normal = glm::normalize(comp.normal);
                        }
                    }
                    ImGui::DragFloat("Height", &comp.height, 0.1f, 0.0f, 100.0f);
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

            // --- 11. Environment Component ---
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

            // --- Component Assignment Menu ---
            if (ImGui::BeginMenu("Add Component...")) {
                addMenuItem((NameComponent*)nullptr, "NameComponent", e);
                addMenuItem((TransformComponent*)nullptr, "TransformComponent", e);
                addMenuItem((RenderComponent*)nullptr, "RenderComponent", e);
                addMenuItem((OrbitComponent*)nullptr, "OrbitComponent", e);
                addMenuItem((ThermoComponent*)nullptr, "ThermoComponent", e);
                addMenuItem((ColliderComponent*)nullptr, "ColliderComponent", e);
                addMenuItem((PhysicsComponent*)nullptr, "PhysicsComponent", e);
                addMenuItem((LightComponent*)nullptr, "LightComponent", e);
                addMenuItem((CameraComponent*)nullptr, "CameraComponent", e);
                addMenuItem((AttachedEmitterComponent*)nullptr, "AttachedEmitterComponent", e);
                addMenuItem((EnvironmentComponent*)nullptr, "EnvironmentComponent", e);
                addMenuItem((DustCloudComponent*)nullptr, "DustCloudComponent", e);
                addMenuItem((LayerRegionComponent*)nullptr, "LayerRegionComponent", e);
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
}

}
