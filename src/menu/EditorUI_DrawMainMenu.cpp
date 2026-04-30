#include "../menu/EditorUI.h"
#include "imgui.h"
#include "../rendering/ParticleLibrary.h"
#include "../systems/PhysicsSystem.h"
#include "../systems/AnimationSystem.h"
#include "../systems/CameraSystem.h"
#include "../systems/ObjectSpawnerSystem.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <random>

void EditorUI::DrawMainMenuSection(float deltaTime, float currentTemp, const std::string& seasonName, Scene& scene, Entity activeOrbitTarget, std::string& sceneToLoad, Entity& entityToDelete) {
    
    
    auto layerMaskToString = [](int mask) {
        std::string s;
        for (int i = 0; i < SceneLayers::ActiveLayerCount; ++i) {
            if ((mask & (1 << i)) != 0) {
                s += "[" + SceneLayers::LayerNames[i] + "] ";
            }
        }
        return s.empty() ? std::string("[None]") : s;
        };
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
        ImGui::Separator();

        ImGui::TextDisabled("Performance");
        if (ImGui::Checkbox("VSync", &m_VSyncEnabled)) {
            m_PerformanceSettingsChanged = true;
        }

        if (ImGui::Checkbox("Enable FPS Cap", &m_FpsCapEnabled)) {
            m_PerformanceSettingsChanged = true;
        }

        if (!m_FpsCapEnabled) {
            ImGui::BeginDisabled();
        }

        if (ImGui::InputInt("Max FPS", &m_MaxFps)) {
            if (m_FpsCapEnabled && m_MaxFps < 1) {
                m_MaxFps = 1;
            }
            else if (!m_FpsCapEnabled && m_MaxFps < 0) {
                m_MaxFps = 0;
            }
            m_PerformanceSettingsChanged = true;
        }

        if (!m_FpsCapEnabled) {
            ImGui::EndDisabled();
        }

        if (ImGui::Button("Force Recreate Swapchain")) {
            m_PerformanceSettingsChanged = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use this if your FPS is locked at 90 unexpectedly.\nForces Vulkan to reset its swapchain and presentation modes.");

        ImGui::Separator();
        ImGui::MenuItem("UI Profiler", nullptr, &m_Profiler.showProfiler);

        ImGui::Separator();

        // --- Layer Manager ---
        if (ImGui::BeginMenu("Layer Properties")) {
            Registry& registry = scene.GetRegistry();
            const Entity entityCount = registry.GetEntityCount();

            bool regionsOnly = scene.GetRegionsOnlyDebugView();
            if (ImGui::Checkbox("Regions Only", &regionsOnly)) {
                scene.SetRegionsOnlyDebugView(regionsOnly);
            }

            ImGui::TextDisabled("Layer regions");
            ImGui::Separator();

            int regionCount = 0;
            static const char* modeItems[] = { "Disabled", "Enabled", "Only In Region" };

            ImGui::Separator();

            for (Entity layerEntity = 0; layerEntity < entityCount; ++layerEntity) {
                if (!registry.HasComponent<LayerRegionComponent>(layerEntity)) continue;
                regionCount++;

                auto& comp = registry.GetComponent<LayerRegionComponent>(layerEntity);
                std::string menuLabel = comp.layerName + "###LayerRegion_" + std::to_string(layerEntity);

                if (ImGui::BeginMenu(menuLabel.c_str())) {
                    ImGui::PushID(static_cast<int>(layerEntity));

                    char nameBuf[64];
                    strncpy_s(nameBuf, comp.layerName.c_str(), sizeof(nameBuf));
                    nameBuf[sizeof(nameBuf) - 1] = '\0';
                    if (ImGui::InputText("Layer Name", nameBuf, sizeof(nameBuf))) {
                        comp.layerName = std::string(nameBuf);
                        if (registry.HasComponent<NameComponent>(layerEntity)) {
                            registry.GetComponent<NameComponent>(layerEntity).name = comp.layerName;
                        }
                        if (comp.assignedLayerBit >= 1 && comp.assignedLayerBit < SceneLayers::MAX_LAYERS) {
                            SceneLayers::LayerNames[comp.assignedLayerBit] = comp.layerName;
                        }
                    }

                    const int oldBit = comp.assignedLayerBit;
                    if (ImGui::SliderInt("Layer Slot", &comp.assignedLayerBit, 1, SceneLayers::MAX_LAYERS - 1)) {
                        comp.assignedLayerBit = std::clamp(comp.assignedLayerBit, 1, SceneLayers::MAX_LAYERS - 1);

                        if (oldBit >= 1 && oldBit < SceneLayers::MAX_LAYERS &&
                            SceneLayers::LayerNames[oldBit] == comp.layerName) {
                            SceneLayers::LayerNames[oldBit] = std::string("Layer ") + static_cast<char>('A' + oldBit);
                        }

                        SceneLayers::LayerNames[comp.assignedLayerBit] = comp.layerName;
                        SceneLayers::ActiveLayerCount = std::max(SceneLayers::ActiveLayerCount, comp.assignedLayerBit + 1);
                    }

                    ImGui::TextDisabled("Slot = Layer %c | Bit = %d | Mask = %d",
                        static_cast<char>('A' + comp.assignedLayerBit),
                        comp.assignedLayerBit,
                        (1 << comp.assignedLayerBit));

                    if (registry.HasComponent<TransformComponent>(layerEntity)) {
                        auto& tr = registry.GetComponent<TransformComponent>(layerEntity);
                        if (ImGui::DragFloat3("Region Position", &tr.position.x, 0.1f)) {
                            tr.UpdateMatrix();
                        }
                    }

                    const char* volumeTypes[] = { "Sphere", "Box (AABB)" };
                    ImGui::Combo("Volume Type", &comp.volumeType, volumeTypes, IM_ARRAYSIZE(volumeTypes));
                    if (comp.volumeType == 0) ImGui::DragFloat("Radius", &comp.radius, 0.1f, 0.1f, 1000.0f);
                    else ImGui::DragFloat3("Half Extents", &comp.halfExtents.x, 0.1f, 0.1f, 1000.0f);

                    ImGui::Separator();
                    ImGui::TextDisabled("Region Debug");
                    ImGui::Checkbox("Show Region", &comp.showRegionDebug);
                    ImGui::ColorEdit4("Region Color", &comp.regionDebugColor.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float);
                    ImGui::SameLine();
                    if (ImGui::Button("Randomise##RegionColorMain")) {
                        static std::mt19937 rng(std::random_device{}());
                        static std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
                        comp.regionDebugColor.r = colorDist(rng);
                        comp.regionDebugColor.g = colorDist(rng);
                        comp.regionDebugColor.b = colorDist(rng);
                        comp.regionDebugColor.a = 0.25f;
                    }

                    ImGui::Separator();
                    ImGui::TextDisabled("Objects Assigned to this Layer");

                    const int bitMask = (1 << comp.assignedLayerBit);
                    if (ImGui::BeginTable("LayerAssignTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                        ImGui::TableHeadersRow();

                        for (Entity other = 0; other < entityCount; ++other) {
                            if (!registry.HasComponent<RenderComponent>(other)) continue;
                            auto& rc = registry.GetComponent<RenderComponent>(other);

                            int mode = 0;
                            if ((rc.onlyInRegionMask & bitMask) != 0) mode = 2;
                            else if ((rc.layerMask & bitMask) != 0) mode = 1;

                            std::string objName = "Entity " + std::to_string(other);
                            if (registry.HasComponent<NameComponent>(other)) {
                                objName = registry.GetComponent<NameComponent>(other).name;
                            }

                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::Text("%s", objName.c_str());

                            ImGui::TableNextColumn();
                            ImGui::PushID(static_cast<int>(other));
                            int newMode = mode;
                            if (ImGui::Combo("##LayerMode", &newMode, modeItems, IM_ARRAYSIZE(modeItems))) {
                                rc.layerMask &= ~bitMask;
                                rc.onlyInRegionMask &= ~bitMask;
                                if (newMode == 1) rc.layerMask |= bitMask;
                                else if (newMode == 2) {
                                    rc.layerMask |= bitMask;
                                    rc.onlyInRegionMask |= bitMask;
                                }
                            }
                            ImGui::PopID();
                        }

                        ImGui::EndTable();
                    }

                    if (ImGui::Button("Delete Layer Region", ImVec2(-1, 0))) {
                        entityToDelete = layerEntity;
                    }

                    ImGui::Separator();
                    if (ImGui::Button("Pop-out to New Window", ImVec2(-1, 0))) {
                        m_PropertyWindows.push_back({ m_NextPropertyWindowId++, layerEntity, false, true, true });
                    }

                    ImGui::PopID();
                    ImGui::EndMenu();
                }
            }

            if (regionCount == 0) {
                ImGui::TextDisabled("No layer regions found.");
            }

            ImGui::Separator();
            static char newLayerName[64] = "New Layer Region";
            static int newLayerSlot = 1;

            ImGui::InputText("New Layer Name", newLayerName, sizeof(newLayerName));
            ImGui::SliderInt("New Layer Slot", &newLayerSlot, 1, SceneLayers::MAX_LAYERS - 1);

            if (ImGui::Button("+ Create Layer Region", ImVec2(-1, 0))) {
                newLayerSlot = std::clamp(newLayerSlot, 1, SceneLayers::MAX_LAYERS - 1);
                scene.AddLayerRegion(newLayerName, newLayerSlot, 0, 10.0f, glm::vec3(5.0f), glm::vec3(0.0f));
                SceneLayers::ActiveLayerCount = std::max(SceneLayers::ActiveLayerCount, newLayerSlot + 1);
            }

            ImGui::EndMenu();
        }


        if (ImGui::MenuItem("Open New Properties Window")) {
            m_PropertyWindows.push_back({ m_NextPropertyWindowId++, MAX_ENTITIES, true, true });
        }

        ImGui::EndMenu();
    }

    DrawLoadSceneMenu(sceneToLoad);

    DrawObjectsMenu(scene, activeOrbitTarget, entityToDelete);

    if (ImGui::BeginMenu("Particles")) {
        const auto& pSystems = scene.GetParticleSystems(); //
        Registry& registry = scene.GetRegistry(); //
        const auto& entities = scene.GetRenderableEntities(); //

        bool hasEmitters = false;
        for (const auto& sys : pSystems) {
            if (!sys->GetEmitters().empty()) { //
                hasEmitters = true;
                break;
            }
        }

        if (!hasEmitters) {
            ImGui::MenuItem("No Active Emitters", nullptr, false, false);
        }
        else {
            for (const auto& sys : pSystems) {
                std::string fullTexPath = sys->GetTexturePath(); //
                std::string texName = fullTexPath;
                size_t slashPos = texName.find_last_of("/\\");
                if (slashPos != std::string::npos) texName = texName.substr(slashPos + 1);

                // Iterate through each emitter managed by this system
                for (const auto& em : sys->GetEmitters()) { //
                    std::string emLabel = "Emitter ID: " + std::to_string(em.id) + " (" + texName + ")##GlobalEm_" + std::to_string(em.id);

                    if (ImGui::BeginMenu(emLabel.c_str())) {
                        ImGui::TextDisabled("Live Controls");
                        ImGui::Separator();

                        // --- LOGARITHMIC EMISSION RATE ---
                        // Provides cubic scaling (x^3) for precise control at low values
                        float rateSlider = std::pow(em.particlesPerSecond / 1000.0f, 1.0f / 3.0f);
                        if (ImGui::SliderFloat("Emission Rate", &rateSlider, 0.0f, 1.0f, "%.1f p/s")) {
                            float newRate = std::pow(rateSlider, 3.0f) * 1000.0f;
                            // Update the system directly using em.props
                            sys->UpdateEmitter(em.id, em.props, newRate);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cubic scale for fine control at low counts.");

                        ImGui::Spacing();
                        ImGui::TextDisabled("Spatial Data");
                        ImGui::Separator();
                        ImGui::Text("Position: (%.1f, %.1f, %.1f)", em.props.position.x, em.props.position.y, em.props.position.z);

                        ImGui::Spacing();
                        ImGui::TextDisabled("Particle Properties");
                        ImGui::Separator();
                        ImGui::Text("Size: %.2f -> %.2f (Var: %.2f)", em.props.sizeBegin, em.props.sizeEnd, em.props.sizeVariation);
                        ImGui::Text("Lifetime: %.2f s", em.props.lifeTime);

                        ImGui::Spacing();
                        ImGui::TextDisabled("Attached To");
                        ImGui::Separator();

                        // Search for entities currently linked to this specific emitter
                        bool foundAttached = false;
                        for (Entity e : entities) {
                            bool attached = false;
                            std::string reason = "";

                            // Check thermal-linked emitters (Fire/Smoke)
                            if (registry.HasComponent<ThermoComponent>(e)) {
                                auto& thermo = registry.GetComponent<ThermoComponent>(e);
                                if (thermo.fireEmitterId == em.id) { attached = true; reason = "Fire"; }
                                if (thermo.smokeEmitterId == em.id) { attached = true; reason += (reason.empty() ? "" : " & ") + std::string("Smoke"); }
                            }

                            // Check general attached emitters
                            if (registry.HasComponent<AttachedEmitterComponent>(e)) {
                                for (const auto& activeEm : registry.GetComponent<AttachedEmitterComponent>(e).emitters) {
                                    if (activeEm.emitterId == em.id) { attached = true; reason = "Custom Emitter"; break; }
                                }
                            }

                            if (attached) {
                                foundAttached = true;
                                std::string name = registry.HasComponent<NameComponent>(e) ? registry.GetComponent<NameComponent>(e).name : "Entity " + std::to_string(e);
                                ImGui::Text(" %s (%s)", name.c_str(), reason.c_str());
                            }
                        }
                        if (!foundAttached) ImGui::Text(" <No Entity Link Found>");

                        ImGui::Separator();
                        if (ImGui::MenuItem("Stop Emitter")) {
                            sys->StopEmitter(em.id); //
                        }

                        ImGui::EndMenu();
                    }
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

                ImGui::Spacing();
                ImGui::TextDisabled("Physics Interaction");
                ImGui::Separator();

                // Safely checks Noclip status for *this specific camera* (e)
                bool isNoclip = CameraSystem::IsNoclip(scene, e);
                if (ImGui::Checkbox("Noclip Enabled", &isNoclip)) {
                    CameraSystem::SetNoclip(scene, isNoclip, e);
                }

                // If Noclip is OFF (meaning it's a bulldozer), let them change the radius
                if (!isNoclip && registry.HasComponent<ColliderComponent>(e)) {
                    auto& col = registry.GetComponent<ColliderComponent>(e);
                    ImGui::DragFloat("Bulldozer Radius", &col.radius, 0.1f, 0.5f, 50.0f);
                }

                auto maskToString = [](int mask) {
                    std::string s;
                    for (int i = 0; i < SceneLayers::ActiveLayerCount; ++i) {
                        if ((mask & (1 << i)) != 0) {
                            s += "[" + SceneLayers::LayerNames[i] + "] ";
                        }
                    }
                    return s.empty() ? std::string("[None]") : s;
                    };

                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Inside Regions: %s", maskToString(cam.insideRegionMask).c_str());
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Viewing Mask:   %s", maskToString(cam.viewMask).c_str());
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

                if (!registry.HasComponent<ObjectSpawnerComponent>(e)) {
                    if (ImGui::MenuItem("+ Attach Spawner to this Camera")) {
                        ObjectSpawnerComponent spawner;
                        spawner.attachToTarget = true;
                        spawner.attachTargetName = baseCamName;
                        spawner.spawnVelocity = glm::vec3(0.0f, 0.0f, 20.0f); // Default forward speed
                        spawner.alwaysOn = false; // Manual/Single fire mode
                        spawner.isRunning = false; // Off by default
                        registry.AddComponent<ObjectSpawnerComponent>(e, spawner);
                    }
                }

                ImGui::Separator();
                if (ImGui::Button("Pop-out to New Window", ImVec2(-1, 0))) {
                    m_PropertyWindows.push_back({ m_NextPropertyWindowId++, e, false, true, true });
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
        bool hasLights = false;

        for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
            if (!registry.HasComponent<LightComponent>(e)) continue;
            hasLights = true;

            auto& light = registry.GetComponent<LightComponent>(e);
            std::string lightName = registry.HasComponent<NameComponent>(e) ?
                registry.GetComponent<NameComponent>(e).name : "Unnamed Light";

            bool isInactive = (light.intensity <= 0.001f);
            if (isInactive) {
                // Change text color to a dim grey for inactive status
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                lightName += " [Inactive]";
            }

            std::string menuLabel = lightName + "###LightMenu_" + std::to_string(e);

            if (ImGui::BeginMenu(menuLabel.c_str())) {
                if (isInactive) ImGui::PopStyleColor(); // Restore color for menu content

                if (registry.HasComponent<TransformComponent>(e)) {
                    auto& transform = registry.GetComponent<TransformComponent>(e);
                    ImGui::TextDisabled("Transform Data");
                    ImGui::Separator();
                    if (ImGui::DragFloat3("Position", &transform.position.x, 0.1f)) {
                        transform.UpdateMatrix();
                    }
                    ImGui::Spacing();
                }

                ImGui::TextDisabled("Light Properties");
                ImGui::Separator();

                ImGui::ColorEdit3("Color", &light.color.x, ImGuiColorEditFlags_Float);

                // --- Logarithmic-mapped Slider (Power 3) ---
                // Mapping: actual_intensity = slider_val^3 * 100
                // This makes 50% on the slider equal to 12.5 intensity.
                float sliderVal = std::pow(light.intensity / 100.0f, 1.0f / 3.0f);
                if (ImGui::SliderFloat("Intensity", &sliderVal, 0.0f, 1.0f, "%.3f")) {
                    light.intensity = std::pow(sliderVal, 3.0f) * 100.0f;
                    if (light.intensity < 0.001f) light.intensity = 0.0f;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Logarithmic scale for finer control at low values (0-100 range)");

                ImGui::Checkbox("Enable Flicker", &light.flickerEnabled);
                ImGui::SliderFloat("Flicker Amount", &light.flickerAmount, 0.0f, 1.0f, "%.2f");

                const char* flickerPresets[] = { "None", "Fire", "Candle", "Faulty", "Pulse" };
                ImGui::Combo("Flicker Preset", &light.flickerPreset, flickerPresets, IM_ARRAYSIZE(flickerPresets));

                if (ImGui::Button("Use Fire Flicker Preset")) {
                    light.flickerEnabled = true;
                    light.flickerPreset = 1;
                    light.flickerAmount = 0.65f;
                }

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
                        if (glm::length(light.direction) > 0.001f) light.direction = glm::normalize(light.direction);
                    }
                    ImGui::SliderFloat("Cone Angle", &light.cutoffAngle, 1.0f, 90.0f, "%.1f deg");
                }

                ImGui::Text("Layer Mask: %s", layerMaskToString(light.layerMask).c_str());

                ImGui::Separator();
                if (ImGui::Button("Pop-out to New Window", ImVec2(-1, 0))) {
                    m_PropertyWindows.push_back({ m_NextPropertyWindowId++, e, false, true, true });
                }

                ImGui::EndMenu();
            }
            else {
                if (isInactive) ImGui::PopStyleColor();
            }
        }

        if (!hasLights) {
            ImGui::MenuItem("No lights in scene", nullptr, false, false);
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Spawners")) {
        Registry& registry = scene.GetRegistry();
        bool hasSpawners = false;

        for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
            if (!registry.HasComponent<ObjectSpawnerComponent>(e)) continue;
            hasSpawners = true;

            auto& spawner = registry.GetComponent<ObjectSpawnerComponent>(e);
            std::string spawnerName = registry.HasComponent<NameComponent>(e)
                ? registry.GetComponent<NameComponent>(e).name
                : ("Spawner " + std::to_string(e));

            std::string menuLabel = spawnerName + "###SpawnerMenu_" + std::to_string(e);
            if (ImGui::BeginMenu(menuLabel.c_str())) {
                ImGui::PushID(static_cast<int>(e));

                const float fireButtonWidth = 88.0f;
                const ImVec4 fireBtn = ImVec4(0.78f, 0.16f, 0.16f, 1.0f);
                const ImVec4 fireBtnHover = ImVec4(0.88f, 0.22f, 0.22f, 1.0f);
                const ImVec4 fireBtnActive = ImVec4(0.62f, 0.10f, 0.10f, 1.0f);

                const char* groupOptions[] = { "A", "B", "C", "D" };
                char groupChar = static_cast<char>(std::toupper(static_cast<unsigned char>(spawner.group)));
                if (groupChar < 'A' || groupChar > 'D') {
                    groupChar = 'A';
                }
                int groupIndex = groupChar - 'A';
                if (ImGui::Combo("Group", &groupIndex, groupOptions, (int)IM_ARRAYSIZE(groupOptions))) {
                    spawner.group = static_cast<char>('A' + groupIndex);
                }

                const bool alwaysOnChanged = ImGui::Checkbox("Always On", &spawner.alwaysOn);
                ImGui::SameLine();
                ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - fireButtonWidth);
                ImGui::PushStyleColor(ImGuiCol_Button, fireBtn);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fireBtnHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, fireBtnActive);
                if (ImGui::Button("Fire", ImVec2(fireButtonWidth, 0))) {
                    ObjectSpawnerSystem::FireOnce(scene, e);
                }
                ImGui::PopStyleColor(3);

                if (spawner.alwaysOn) {
                    spawner.isRunning = true;
                    spawner.runDurationSeconds = -1.0f;
                    spawner.maxSpawnsPerRun = -1;
                }
                else if (alwaysOnChanged) {
                    spawner.spawnTimer = 0.0f;
                    spawner.runElapsedSeconds = 0.0f;
                    spawner.spawnedThisRun = 0;
                }

                ImGui::BeginDisabled(spawner.alwaysOn);
                if (!spawner.isRunning) {
                    if (ImGui::Button("Run")) {
                        spawner.isRunning = true;
                        spawner.spawnTimer = 0.0f;
                        spawner.runElapsedSeconds = 0.0f;
                        spawner.spawnedThisRun = 0;
                    }
                }
                else {
                    if (ImGui::Button("Stop")) {
                        spawner.isRunning = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset Run")) {
                    spawner.spawnTimer = 0.0f;
                    spawner.runElapsedSeconds = 0.0f;
                    spawner.spawnedThisRun = 0;
                }
                ImGui::EndDisabled();

                ImGui::DragFloat("Spawn Interval (s)", &spawner.spawnInterval, 0.05f, 0.05f, 60.0f);
                ImGui::BeginDisabled(spawner.alwaysOn);
                ImGui::DragFloat("Run Duration (s, -1 inf)", &spawner.runDurationSeconds, 0.1f, -1.0f, 3600.0f);
                ImGui::InputInt("Max Spawns Per Run (-1 inf)", &spawner.maxSpawnsPerRun);
                ImGui::EndDisabled();
                if (spawner.maxSpawnsPerRun < -1) {
                    spawner.maxSpawnsPerRun = -1;
                }

                ImGui::DragFloat("Object Scale", &spawner.spawnObjectScale, 0.05f, 0.05f, 100.0f);
                ImGui::DragFloat3("Object Scale XYZ", &spawner.spawnScale.x, 0.05f, 0.05f, 100.0f);
                ImGui::DragFloat("Spawn Mass", &spawner.spawnMass, 0.1f, 0.01f, 1000.0f);
                ImGui::DragFloat("Spawn Lifespan (s, -1 inf)", &spawner.spawnLifespanSeconds, 0.1f, -1.0f, 600.0f);

                int geometryIndex = 0;
                const char* geometryTypes[] = { "Sphere", "Cube", "Plane", "Model", "Smoke Grenade" };
                if (spawner.spawnGeometryType == "Cube") geometryIndex = 1;
                else if (spawner.spawnGeometryType == "Plane") geometryIndex = 2;
                else if (spawner.spawnGeometryType == "Model") geometryIndex = 3;
                else if (spawner.spawnGeometryType == "Smoke Grenade") geometryIndex = 4;

                if (ImGui::Combo("Spawn Geometry", &geometryIndex, geometryTypes, (int)IM_ARRAYSIZE(geometryTypes))) {
                    spawner.spawnGeometryType = geometryTypes[geometryIndex];
                    if (spawner.spawnGeometryType == "Sphere") {
                        spawner.spawnObjectScale = std::max(0.05f, std::max({ spawner.spawnScale.x, spawner.spawnScale.y, spawner.spawnScale.z }));
                    }
                }

                if (spawner.spawnGeometryType == "Model") {
                    std::string modelPreview = spawner.spawnModelPath.empty() ? "Select model..." : spawner.spawnModelPath;
                    if (ImGui::BeginCombo("Spawn Model", modelPreview.c_str())) {
                        for (const auto& modelPath : m_AvailableModels) {
                            bool selected = (spawner.spawnModelPath == modelPath);
                            if (ImGui::Selectable(modelPath.c_str(), selected)) {
                                spawner.spawnModelPath = modelPath;
                            }
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }

                std::string texPreview = spawner.spawnTexturePath.empty() ? "Select texture..." : spawner.spawnTexturePath;
                if (ImGui::BeginCombo("Spawn Texture", texPreview.c_str())) {
                    for (const auto& texPath : m_AvailableTextures) {
                        bool selected = (spawner.spawnTexturePath == texPath);
                        if (ImGui::Selectable(texPath.c_str(), selected)) {
                            spawner.spawnTexturePath = texPath;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::TreeNode("Generate Procedural Texture##ProcSpawner")) {
                    static char spawnerProcName[64] = "spawner_proc_tex";
                    static int spawnerProcType = 1;
                    static glm::vec4 spawnerColor1(1.0f, 1.0f, 1.0f, 1.0f);
                    static glm::vec4 spawnerColor2(0.2f, 0.2f, 0.2f, 1.0f);
                    static int spawnerCellSize = 32;

                    auto queueSpawnerProcUpdate = [&]() {
                        if (spawnerProcName[0] == '\0') return;
                        ProceduralTextureRequest req;
                        req.name = std::string(spawnerProcName);
                        req.type = static_cast<ProcTexType>(spawnerProcType);
                        req.color1 = spawnerColor1;
                        req.color2 = spawnerColor2;
                        req.cellSize = std::max(1, spawnerCellSize);
                        m_TextureRequests.push_back(req);
                        spawner.spawnTexturePath = req.name;
                    };

                    bool spSpawnerChanged = false;
                    spSpawnerChanged |= ImGui::InputText("Name ID", spawnerProcName, sizeof(spawnerProcName));
                    const char* procTypes[] = { "Solid Color", "Checkerboard", "Gradient (Vert)", "Gradient (Horiz)" };
                    spSpawnerChanged |= ImGui::Combo("Type", &spawnerProcType, procTypes, (int)IM_ARRAYSIZE(procTypes));
                    spSpawnerChanged |= ImGui::ColorEdit4("Color 1", &spawnerColor1.x);
                    if (spawnerProcType > 0) spSpawnerChanged |= ImGui::ColorEdit4("Color 2", &spawnerColor2.x);
                    if (spawnerProcType == 1) spSpawnerChanged |= ImGui::InputInt("Cell Size", &spawnerCellSize);

                    if (ImGui::Button("Apply Procedural Texture")) {
                        queueSpawnerProcUpdate();
                    }
                    if (spSpawnerChanged) {
                        queueSpawnerProcUpdate();
                    }
                    ImGui::TreePop();
                }

                ImGui::Separator();
                ImGui::DragFloat3("Base Velocity", &spawner.spawnVelocity.x, 0.1f);
                ImGui::Checkbox("Randomise Velocity", &spawner.randomizeVelocity);
                if (spawner.randomizeVelocity) {
                    ImGui::DragFloat3("Random Velocity Range", &spawner.randomVelocityRange.x, 0.1f, 0.0f, 200.0f);
                }

                ImGui::DragFloat3("Base Spin", &spawner.spawnAngularVelocity.x, 0.05f);
                ImGui::Checkbox("Randomise Spin", &spawner.randomizeAngularVelocity);
                if (spawner.randomizeAngularVelocity) {
                    ImGui::DragFloat3("Random Spin Range", &spawner.randomAngularVelocityRange.x, 0.05f, 0.0f, 50.0f);
                }

                ImGui::Separator();
                ImGui::TextDisabled("Entity Attachment");
                ImGui::Checkbox("Attach to Target", &spawner.attachToTarget);
                if (spawner.attachToTarget) {
                    char targetBuf[64];
                    strncpy_s(targetBuf, spawner.attachTargetName.c_str(), sizeof(targetBuf));
                    targetBuf[sizeof(targetBuf) - 1] = '\0';
                    if (ImGui::InputText("Target Name", targetBuf, sizeof(targetBuf))) {
                        spawner.attachTargetName = std::string(targetBuf);
                    }
                    ImGui::TextDisabled("Empty = Active Camera");
                }

                ImGui::Separator();
                ImGui::TextDisabled("Status: %s", spawner.isRunning ? "Running" : "Stopped");
                ImGui::TextDisabled("Spawned This Run: %d", spawner.spawnedThisRun);
                ImGui::TextDisabled("Total Spawned: %d", spawner.spawnedCount);

                ImGui::Separator();
                if (ImGui::Button("Pop-out to New Window", ImVec2(-1, 0))) {
                    m_PropertyWindows.push_back({ m_NextPropertyWindowId++, e, false, true, true });
                }

                ImGui::PopID();
                ImGui::EndMenu();
            }
        }

        if (!hasSpawners) {
            ImGui::MenuItem("No spawners in scene", nullptr, false, false);
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Refresh Model List")) {
            RefreshModelList();
        }
        if (ImGui::MenuItem("Refresh Texture List")) {
            RefreshTextureList();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("+ Create New Spawner")) {
            scene.AddSpawner("New Spawner", glm::vec3(0.0f, 10.0f, 0.0f));
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

        if (ImGui::CollapsingHeader("Physics Engine", ImGuiTreeNodeFlags_DefaultOpen)) {

            ImGui::Text("Time Step & Substepping");
            // Slider to control how many times the physics loop runs per frame
            ImGui::SliderInt("Substeps per Frame", &PhysicsSystem::subSteps, 1, 16);

            ImGui::Spacing();
            ImGui::Text("Integration Method");

            // Dropdown for Integration Method
            int currentMethodIdx = static_cast<int>(PhysicsSystem::currentMethod);
            const char* methods[] = { "Explicit Euler", "Semi-Implicit Euler", "RK4" };
            if (ImGui::Combo("Algorithm", &currentMethodIdx, methods, IM_ARRAYSIZE(methods))) {
                PhysicsSystem::currentMethod = static_cast<IntegrationMethod>(currentMethodIdx);
            }

            ImGui::Spacing();
            ImGui::SliderInt("Sub-steps", &PhysicsSystem::subSteps, 1, 32);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Higher values increase physics precision and reduce clipping of fast objects, but cost performance.");
            }

            ImGui::Spacing();
            // Checkbox to disable gravity to prove the zero-acceleration lab requirement

            ImGui::Text("Collision Resolution");

            int currentResMethodIdx = static_cast<int>(PhysicsSystem::currentResolutionMethod);
            const char* resMethods[] = { "Impulse (Velocity)", "Force Accumulation" };
            if (ImGui::Combo("Resolution Type", &currentResMethodIdx, resMethods, IM_ARRAYSIZE(resMethods))) {
                PhysicsSystem::currentResolutionMethod = static_cast<ResolutionMethod>(currentResMethodIdx);
            }
            ImGui::Spacing();

            ImGui::Checkbox("Apply Gravity", &PhysicsSystem::applyGravity);

            if (PhysicsSystem::applyGravity) {
                ImGui::SameLine();
                std::string dirLabel = PhysicsSystem::gravityDirection < 0.0f ? "Flip Up" : "Flip Down";
                if (ImGui::Button(dirLabel.c_str())) {
                    PhysicsSystem::gravityDirection *= -1.0f;
                }
                
                // ADDED GRAVITY SLIDER HERE
                float currentGravity = std::abs(PhysicsSystem::gravityDirection);
                if (ImGui::SliderFloat("Gravity Force", &currentGravity, 0.0f, 50.0f, "%.2f m/s^2")) {
                    // Preserve the direction (sign) while applying the new magnitude
                    PhysicsSystem::gravityDirection = (PhysicsSystem::gravityDirection < 0.0f) ? -currentGravity : currentGravity;
                }
            }

            ImGui::Spacing();
            ImGui::Text("Contact Materials");
            ImGui::SliderFloat("Global Friction Scale", &PhysicsSystem::contactFrictionScale, 0.0f, 5.0f, "%.2fx");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Scales grip between colliding objects. Higher values transfer more spin.");
            }

            ImGui::Spacing();
            ImGui::Text("Air Resistance / Damping");

            if (ImGui::Checkbox("Linear Damping", &m_LinearDampingEnabled)) {
                m_PhysicsSettingsChanged = true;
            }

            if (m_LinearDampingEnabled) {
                if (ImGui::SliderFloat("Damping Factor", &m_LinearDampingFactor, 0.9f, 1.0f, "%.3f")) {
                    m_PhysicsSettingsChanged = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher values = less damping (1.0 = no damping)");
            }

            if (ImGui::Checkbox("Quadratic Drag", &m_QuadraticDragEnabled)) {
                m_PhysicsSettingsChanged = true;
            }

            if (m_QuadraticDragEnabled) {
                if (ImGui::SliderFloat("Drag Coefficient", &m_QuadraticDragCoeff, 0.0f, 0.1f, "%.4f")) {
                    m_PhysicsSettingsChanged = true;
                }
            }

            ImGui::Spacing();
            if (ImGui::Checkbox("Visualize Springs", &m_ShowSpringVisuals)) {
                m_SpringVisualizationChanged = true;
            }

            if (ImGui::Checkbox("Visualize Spawners", &m_ShowSpawnerVisuals)) {
                m_SpawnerVisualizationChanged = true;
            }

            Registry& simRegistry = scene.GetRegistry();
            const Entity simEntityCount = simRegistry.GetEntityCount();
            bool hasPathAnimations = false;
            bool allPathsShown = true;
            for (Entity e = 0; e < simEntityCount; ++e) {
                if (!simRegistry.HasComponent<PathAnimationComponent>(e)) {
                    continue;
                }

                hasPathAnimations = true;
                if (!simRegistry.GetComponent<PathAnimationComponent>(e).showPath) {
                    allPathsShown = false;
                }
            }

            bool showAllPaths = allPathsShown;
            if (!hasPathAnimations) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Checkbox("Visualize Animation Paths", &showAllPaths)) {
                for (Entity e = 0; e < simEntityCount; ++e) {
                    if (simRegistry.HasComponent<PathAnimationComponent>(e)) {
                        simRegistry.GetComponent<PathAnimationComponent>(e).showPath = showAllPaths;
                    }
                }
            }
            if (!hasPathAnimations) {
                ImGui::EndDisabled();
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Replay")) {
        if (!m_IsReplaying) {
            ImGui::SliderFloat("Timeframe (s)", &m_LookaheadTimeframe, 1.0f, 60.0f, "%.1f");
            ImGui::Separator();
            if (ImGui::MenuItem("Generate Lookahead")) {
                m_GenerateLookaheadRequested = true;
                m_ReplayFreeRoam = true;
            }
        } else {
            ImGui::TextDisabled("Replay in Progress...");
            if (ImGui::MenuItem("Stop Replay")) {
                m_IsReplaying = false;
                m_ReplayPlaying = false;
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Animations")) {
        Registry& registry = scene.GetRegistry();
        const Entity entityCount = registry.GetEntityCount();

        int animationCount = 0;
        for (Entity e = 0; e < entityCount; ++e) {
            if (registry.HasComponent<PathAnimationComponent>(e)) {
                ++animationCount;
            }
        }

        if (animationCount == 0) {
            ImGui::TextDisabled("No path animations found.");
        }
        else {
            ImGui::Text("Animations: %d", animationCount);

            if (ImGui::Button("Play All", ImVec2(-1, 0))) {
                for (Entity e = 0; e < entityCount; ++e) {
                    if (registry.HasComponent<PathAnimationComponent>(e)) {
                        registry.GetComponent<PathAnimationComponent>(e).isPlaying = true;
                    }
                }
            }

            if (ImGui::Button("Pause All", ImVec2(-1, 0))) {
                for (Entity e = 0; e < entityCount; ++e) {
                    if (registry.HasComponent<PathAnimationComponent>(e)) {
                        registry.GetComponent<PathAnimationComponent>(e).isPlaying = false;
                    }
                }
            }

            if (ImGui::Button("Rewind All", ImVec2(-1, 0))) {
                for (Entity e = 0; e < entityCount; ++e) {
                    if (registry.HasComponent<PathAnimationComponent>(e)) {
                        auto& path = registry.GetComponent<PathAnimationComponent>(e);
                        path.reversePath = true;
                        path.isPlaying = true;
                    }
                }
            }

            if (ImGui::Button("Play Forward All", ImVec2(-1, 0))) {
                for (Entity e = 0; e < entityCount; ++e) {
                    if (registry.HasComponent<PathAnimationComponent>(e)) {
                        auto& path = registry.GetComponent<PathAnimationComponent>(e);
                        path.reversePath = false;
                        path.isPlaying = true;
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Text("Global Animation Speed");
            ImGui::SliderFloat("##GlobalAnimSpeed", &AnimationSystem::globalPlaybackSpeed, 0.0f, 5.0f, "%.2fx");
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Environment")) {
        Registry& sceneRegistry = scene.GetRegistry();
        Entity envEntity = scene.GetEnvironmentEntity();
        const bool hasEnvironment =
            (envEntity != MAX_ENTITIES) &&
            sceneRegistry.HasComponent<EnvironmentComponent>(envEntity);



        if (!hasEnvironment) {
            ImGui::TextDisabled("No environment component found.");
            ImGui::Separator();
            if (ImGui::Selectable("Initialise Environment", false, ImGuiSelectableFlags_DontClosePopups)) {
                scene.CreateEnvironment();
            }
        }
        else {
            ImGui::TextDisabled("Live Status");
            ImGui::Separator();
            ImGui::Text("Season: %s", seasonName.c_str());
            ImGui::Text("Global Temp: %.1f C", currentTemp);

            auto& env = sceneRegistry.GetComponent<EnvironmentComponent>(envEntity);
            ImGui::Text("Sun Heat Bonus: %.1f", env.sunHeatBonus);
            ImGui::Text("Weather Intensity: %.2f", env.weatherIntensity);
            ImGui::Text("Time Since Rain: %.1f s", env.timeSinceLastRain);
            ImGui::Text("Fire Suppression Timer: %.1f s", env.postRainFireSuppressionTimer);


            ImGui::Spacing();
            ImGui::TextDisabled("Controls");
            ImGui::Separator();

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
        }
        ImGui::EndMenu();
    }

    DrawMainMenuStatusBar(deltaTime);
    ImGui::EndMainMenuBar();
}

}

void EditorUI::DrawLoadSceneMenu(std::string& sceneToLoad) {
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
}

void EditorUI::DrawMainMenuStatusBar(float deltaTime) {
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
}

void EditorUI::DrawObjectsMenu(Scene& scene, Entity activeOrbitTarget, Entity& entityToDelete) {
    auto getLayerModeForBit = [](const RenderComponent& rc, int bit) -> int {
        const int bitMask = (1 << bit);
        const bool visible = (rc.layerMask & bitMask) != 0;
        const bool only = (rc.onlyInRegionMask & bitMask) != 0;

        if (only) return 2;     // Only Display In Region
        if (visible) return 1;  // Enabled
        return 0;               // Disabled
        };

    auto setLayerModeForBit = [](RenderComponent& rc, int bit, int mode) {
        const int bitMask = (1 << bit);

        rc.layerMask &= ~bitMask;
        rc.onlyInRegionMask &= ~bitMask;

        switch (mode) {
        case 1: // Enabled
            rc.layerMask |= bitMask;
            break;
        case 2: // Only Display In Region
            rc.layerMask |= bitMask;
            rc.onlyInRegionMask |= bitMask;
            break;
        default: // Disabled
            break;
        }
        };

    if (ImGui::BeginMenu("Objects")) {
        auto layerMaskToString = [](int mask) {
            std::string s;
            for (int i = 0; i < SceneLayers::ActiveLayerCount; ++i) {
                if ((mask & (1 << i)) != 0) {
                    s += "[" + SceneLayers::LayerNames[i] + "] ";
                }
            }
            return s.empty() ? std::string("[None]") : s;
        };

        Registry& registry = scene.GetRegistry();

        ImGui::TextDisabled("Cloth Systems");
        ImGui::Separator();
        if (ImGui::BeginMenu("Cloth Grids")) {
            bool hasCloth = false;

            for (Entity e = 0; e < registry.GetEntityCount(); ++e) {
                if (!registry.HasComponent<ClothComponent>(e)) continue;
                hasCloth = true;

                auto& comp = registry.GetComponent<ClothComponent>(e);
                std::string clothName = registry.HasComponent<NameComponent>(e) ?
                    registry.GetComponent<NameComponent>(e).name : "Cloth " + std::to_string(e);

                std::string menuLabel = clothName + "###ClothMenu_" + std::to_string(e);

                if (ImGui::BeginMenu(menuLabel.c_str())) {
                    ImGui::PushID(static_cast<int>(e));

                    ImGui::TextDisabled("Grid Details");
                    ImGui::Separator();
                    ImGui::Text("Grid Size: %d x %d", comp.width, comp.height);
                    ImGui::Text("Spacing: %.2f", comp.spacing);
                    ImGui::Text("Particles: %zu", comp.particles.size());

                    ImGui::Spacing();
                    ImGui::TextDisabled("Physical Properties");
                    ImGui::Separator();

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

                    ImGui::Separator();
                    if (ImGui::Button("Pop-out to New Window", ImVec2(-1, 0))) {
                        m_PropertyWindows.push_back({ m_NextPropertyWindowId++, e, false, true, true });
                    }

                    ImGui::PopID();
                    ImGui::EndMenu();
                }
            }

            if (!hasCloth) {
                ImGui::MenuItem("No cloth grids in scene", nullptr, false, false);
            }

            ImGui::EndMenu();
        }
        
        ImGui::Spacing();
        ImGui::TextDisabled("Entities");
        ImGui::Separator();
        const auto& entities = scene.GetRenderableEntities();

        std::vector<Entity> springVisualEntities;
        std::vector<Entity> pathVisualEntities;
        std::vector<Entity> spawnerVisualEntities;

        auto isVisualHelper = [&](Entity entity) {
            if (!registry.HasComponent<RenderComponent>(entity)) {
                return false;
            }

            const auto& rc = registry.GetComponent<RenderComponent>(entity);
            if (rc.geometryName == "spring_visual") {
                springVisualEntities.push_back(entity);
                return true;
            }
            if (rc.geometryName == "path_visual") {
                pathVisualEntities.push_back(entity);
                return true;
            }
            if (rc.geometryName == "spawner_visual") {
                spawnerVisualEntities.push_back(entity);
                return true;
            }

            return false;
        };

        auto selectEntityInProperties = [&](Entity target) {
            if (m_PropertyWindows.empty()) {
                m_PropertyWindows.push_back({ m_NextPropertyWindowId++, target, true, true, false });
                return;
            }

            m_PropertyWindows.front().selectedEntity = target;
            m_PropertyWindows.front().isOpen = true;
        };

        if (entities.empty()) {
            ImGui::MenuItem("No objects in scene", nullptr, false, false);
        }
        else {
            for (Entity e : entities) {
                if (isVisualHelper(e)) {
                    continue;
                }

                std::string entityName = "Entity " + std::to_string(e);
                if (registry.HasComponent<NameComponent>(e)) {
                    entityName = registry.GetComponent<NameComponent>(e).name;
                }

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

                if (registry.HasComponent<AttachedEmitterComponent>(e)) {
                    emitterCount += static_cast<int>(registry.GetComponent<AttachedEmitterComponent>(e).emitters.size());
                }

                bool isViewing = (e == activeOrbitTarget && activeOrbitTarget != MAX_ENTITIES);

                if (isViewing) {
                    entityName += " [VIEWING]";
                }
                if (emitterCount > 0) {
                    entityName += " [" + std::to_string(emitterCount) + " Emitters]";
                }

                int popCount = 0;
                if (isViewing) {
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

                std::string menuLabel = entityName + "###ObjMenu_" + std::to_string(e);

                bool menuOpen = ImGui::BeginMenu(menuLabel.c_str());

                if (popCount > 0) {
                    ImGui::PopStyleColor(popCount);
                }

                if (menuOpen) {
                    ImGui::TextDisabled("Entity Properties");

                    const bool canViewObject = registry.HasComponent<TransformComponent>(e);
                    if (!canViewObject) ImGui::BeginDisabled();
                    if (ImGui::Button("View Object", ImVec2(-1, 0)) && canViewObject) {
                        m_ViewRequested = e;
                        selectEntityInProperties(e);
                    }
                    if (!canViewObject) {
                        ImGui::EndDisabled();
                        ImGui::TextDisabled("View requires TransformComponent");
                    }

                    if (ImGui::Button("Delete Entity", ImVec2(-1, 0))) {
                        entityToDelete = e;
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

                            ImGui::PushID((int)i);
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

                            newEm.props.position = pos;
                            newEm.emitterId = scene.GetOrCreateSystem(newEm.props)->AddEmitter(newEm.props, rate);
                            attached.emitters.push_back(newEm);
                        };

                        auto presets = ParticleLibrary::GetAllPresets();
                        for (const auto& [name, props] : presets) {
                            if (ImGui::MenuItem(name.c_str())) {
                                attachFunc(props, 100.0f);
                            }
                        }
                        ImGui::EndMenu();
                    }

                    ImGui::Separator();

                    if (registry.HasComponent<TransformComponent>(e)) {
                        glm::vec3 pos = registry.GetComponent<TransformComponent>(e).matrix[3];
                        ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);
                    }

                    if (registry.HasComponent<RenderComponent>(e)) {
                        auto& render = registry.GetComponent<RenderComponent>(e);
                        ImGui::Text("Layer Mask: %s", layerMaskToString(render.layerMask).c_str());

                        const char* modes[] = { "None", "Phong", "Gouraud", "Flat", "Wireframe" };
                        const char* modeName = (render.shadingMode >= 0 && render.shadingMode <= 4) ? modes[render.shadingMode] : "Unknown";
                        ImGui::Text("Shading: %s", modeName);

                        ImGui::Separator();
                        ImGui::TextDisabled("Layer Options");

                        if (ImGui::BeginTable("ObjLayerOptionsTable", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 140.0f);
                            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableHeadersRow();

                            static const char* modeItems[] = {
                                "Disabled",
                                "Enabled",
                                "Only Display In Region"
                            };

                            for (int bit = 0; bit < SceneLayers::ActiveLayerCount; ++bit) {
                                ImGui::TableNextRow();

                                ImGui::TableNextColumn();
                                ImGui::Text("%s", SceneLayers::LayerNames[bit].c_str());

                                ImGui::TableNextColumn();
                                int mode = getLayerModeForBit(render, bit);

                                ImGui::PushID(bit);
                                if (ImGui::Combo("##ObjLayerMode", &mode, modeItems, IM_ARRAYSIZE(modeItems))) {
                                    setLayerModeForBit(render, bit, mode);
                                }
                                ImGui::PopID();
                            }

                            ImGui::EndTable();
                        }
                    }

                    if (registry.HasComponent<ThermoComponent>(e)) {
                        auto& thermo = registry.GetComponent<ThermoComponent>(e);
                        ImGui::Text("Temp: %.1f C", thermo.currentTemp);
                        if (thermo.state == ObjectState::BURNING) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "STATE: BURNING");
                        }
                    }

                    if (registry.HasComponent<RenderComponent>(e)) {
                        auto& render = registry.GetComponent<RenderComponent>(e);
                        ImGui::Separator();
                        ImGui::TextDisabled("Material");

                        std::string textureMenuLabel =
                            "Current Texture: " + render.texturePath + "###ObjTexMenu_" + std::to_string(e);

                        if (ImGui::BeginMenu(textureMenuLabel.c_str())) {
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

                            if (ImGui::BeginMenu("Generate Procedural Texture##Obj")) {
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

                                    render.texturePath = req.name;
                                    };

                                bool changed = false;

                                changed |= ImGui::InputText("Name ID", procName, sizeof(procName));

                                const char* procTypes[] = { "Solid Color", "Checkerboard", "Gradient (Vert)", "Gradient (Horiz)" };
                                changed |= ImGui::Combo("Type", &procType, procTypes, IM_ARRAYSIZE(procTypes));

                                changed |= ImGui::ColorEdit4("Color 1", &color1.x);
                                if (procType > 0) {
                                    changed |= ImGui::ColorEdit4("Color 2", &color2.x);
                                }
                                if (procType == 1) {
                                    changed |= ImGui::InputInt("Cell Size", &cellSize);
                                }

                                if (ImGui::Button("Randomise##ProcTexObj")) {
                                    procType = typeDist(rng);
                                    color1 = glm::vec4(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);
                                    color2 = glm::vec4(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);
                                    if (procType == 1) {
                                        cellSize = cellDist(rng);
                                    }
                                    queueProceduralUpdate();
                                }

                                // Rebuild immediately whenever any control changes
                                if (changed) {
                                    queueProceduralUpdate();
                                }

                                ImGui::EndMenu();
                            }

                            ImGui::EndMenu();
                        }
                    }
                    

                    ImGui::Spacing();

                    ImGui::Spacing();

                    if (registry.HasComponent<RenderComponent>(e)) {
                        auto& render = registry.GetComponent<RenderComponent>(e);

                        std::string geometryMenuLabel =
                            "Current Geometry: " + render.geometryName + "###ChangeGeometryMenu_" + std::to_string(e);

                        if (ImGui::BeginMenu(geometryMenuLabel.c_str())) {
                            static int geoTypeIdx = 0;
                            const char* geoTypes[] = { "Model File", "Cube", "Sphere", "Plane", "Cylinder", "Bowl", "Terrain", "Disk", "Grid" };
                            ImGui::Combo("Shape Type", &geoTypeIdx, geoTypes, (int)IM_ARRAYSIZE(geoTypes));

                            static std::string selectedModel = "";
                            if (geoTypeIdx == 0) {
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
                    }

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

                    ImGui::Separator();
                    if (ImGui::Button("Pop-out to New Window", ImVec2(-1, 0))) {
                        m_PropertyWindows.push_back({ m_NextPropertyWindowId++, e, false, true, true });
                    }

                    ImGui::EndMenu();
                }
            }
        }

        if (!springVisualEntities.empty() || !pathVisualEntities.empty() || !spawnerVisualEntities.empty()) {
            ImGui::Separator();
            if (ImGui::BeginMenu("Visualization Helpers")) {
                auto drawVisualList = [&](const char* title, const std::vector<Entity>& visualEntities) {
                    if (ImGui::BeginMenu(title)) {
                        for (Entity helperEntity : visualEntities) {
                            std::string helperName = "Entity " + std::to_string(helperEntity);
                            if (registry.HasComponent<NameComponent>(helperEntity)) {
                                helperName = registry.GetComponent<NameComponent>(helperEntity).name;
                            }

                            std::string helperMenuLabel = helperName + "###VisualHelper_" + std::to_string(helperEntity);
                            if (ImGui::BeginMenu(helperMenuLabel.c_str())) {
                                if (ImGui::Button("View Object", ImVec2(-1, 0))) {
                                    m_ViewRequested = helperEntity;
                                    selectEntityInProperties(helperEntity);
                                }

                                if (ImGui::Button("Open in Properties", ImVec2(-1, 0))) {
                                    selectEntityInProperties(helperEntity);
                                }

                                ImGui::EndMenu();
                            }
                        }
                        ImGui::EndMenu();
                    }
                };

                drawVisualList("Spring Visuals", springVisualEntities);
                drawVisualList("Path Visuals", pathVisualEntities);
                drawVisualList("Spawner Visuals", spawnerVisualEntities);
                ImGui::EndMenu();
            }
        }

        ImGui::EndMenu();
    }
}
