#include "EditorUI.h"
#include "imgui.h"

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

std::string EditorUI::Draw(float deltaTime, float currentTemp, const std::string& seasonName, const Scene& scene) {
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
            const auto& objects = scene.GetObjects();
            if (objects.empty()) {
                ImGui::MenuItem("No objects in scene", nullptr, false, false);
            }
            else {
                for (const auto& obj : objects) {
                    if (!obj) continue;

                    // Using BeginMenu here creates the submenu for each object
                    if (ImGui::BeginMenu(obj->name.empty() ? "Unnamed Object" : obj->name.c_str())) {

                        // Header for visual clarity
                        ImGui::TextDisabled("Object Properties");
                        ImGui::Separator();

                        // 1. Transform / Position
                        glm::vec3 pos = obj->transform[3]; // Extract position column
                        ImGui::Text("Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

                        // 2. Layer Information
                        const char* layerName = (obj->layerMask & SceneLayers::INSIDE) ? "Inside" : "Outside";
                        ImGui::Text("Layer: %s", layerName);

                        // 3. Shading Mode
                        const char* modes[] = { "None", "Phong", "Gouraud", "Flat", "Wireframe" };
                        const char* modeName = (obj->shadingMode >= 0 && obj->shadingMode <= 4) ? modes[obj->shadingMode] : "Unknown";
                        ImGui::Text("Shading: %s", modeName);

                        // 4. Thermodynamics
                        ImGui::Text("Temp: %.1f C", obj->currentTemp);
                        if (obj->state == ObjectState::BURNING) {
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f), "STATE: BURNING");
                        }

                        // 5. Material
                        ImGui::Separator();
                        ImGui::Text("Texture:");
                        ImGui::TextWrapped("%s", obj->texturePath.c_str());

                        ImGui::EndMenu(); // Close the object's submenu
                    }
                }
            }
            ImGui::EndMenu(); // Close the "Objects" tab
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