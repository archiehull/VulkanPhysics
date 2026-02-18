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

std::string EditorUI::Draw(float deltaTime, float currentTemp, const std::string& seasonName) {
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

        // --- TAB: Environment (Left Aligned) ---
        if (ImGui::BeginMenu("Environment")) {
            ImGui::MenuItem((std::string("Season: ") + seasonName).c_str(), nullptr, false, false);
            ImGui::MenuItem((std::string("Temp: ") + std::to_string((int)currentTemp) + " C").c_str(), nullptr, false, false);
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

    return sceneToLoad;
}