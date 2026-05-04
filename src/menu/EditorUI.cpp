#include "EditorUI.h"
#include "imgui.h"
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

NetworkSettingsRequest EditorUI::ConsumeNetworkSettingsRequest() {
    NetworkSettingsRequest req = m_PendingNetworkSettings;
    // Reset the flag so we don't apply it every single frame
    m_PendingNetworkSettings.applyRequested = false;
    return req;
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
    auto totalStart = std::chrono::high_resolution_clock::now();
    std::string sceneToLoad = "";
    Entity entityToDelete = MAX_ENTITIES;

    ImGui::GetIO().FontGlobalScale = m_UIScale;

    // Draw main menu first (top bar)
    auto mainMenuStart = std::chrono::high_resolution_clock::now();
    DrawMainMenuSection(deltaTime, currentTemp, seasonName, scene, activeOrbitTarget, sceneToLoad, entityToDelete);
    auto mainMenuEnd = std::chrono::high_resolution_clock::now();
    m_Profiler.drawMainMenuTime = std::chrono::duration<float, std::milli>(mainMenuEnd - mainMenuStart).count();

    // Property windows
    auto windowsStart = std::chrono::high_resolution_clock::now();
    DrawPropertyWindowsSection(scene, entityToDelete);
    auto windowsEnd = std::chrono::high_resolution_clock::now();
    m_Profiler.drawWindowsTime = std::chrono::duration<float, std::milli>(windowsEnd - windowsStart).count();

    // Post main-menu section performs cleanup actions such as handling entity deletions
    DrawPostMainMenuSection(scene, entityToDelete);

    DrawNetworkWindow();

    if (m_IsReplaying) {
        DrawReplayEditor(scene);

        if (m_ReplayPlaying) {
            m_ReplayAccumulator += deltaTime * m_ReplayPlaybackSpeed;
            float frameDuration = m_StepSize; // Assuming snapshots are taken at m_StepSize intervals
            while (m_ReplayAccumulator >= frameDuration) {
                m_ReplayAccumulator -= frameDuration;
                m_CurrentReplayFrame++;
                if (m_CurrentReplayFrame >= m_MaxReplayFrames) {
                    m_CurrentReplayFrame = 0; // Loop replay
                }
            }
        }
    }

    auto totalEnd = std::chrono::high_resolution_clock::now();
    m_Profiler.totalTime = std::chrono::duration<float, std::milli>(totalEnd - totalStart).count();

    return sceneToLoad;
}

void EditorUI::SetUpdateTime(float ms) {
    m_Profiler.updateTime = ms;
}

void EditorUI::SetPhysicsTime(float ms) {
    m_Profiler.physicsTime = ms;
}

void EditorUI::SetRenderTime(float ms) {
    m_Profiler.renderTime = ms;
}

void EditorUI::SetThreadInfo(int count, unsigned long affinityMask) {
    m_Profiler.threadCount = count;
    m_Profiler.threadAffinityMask = affinityMask;
}

void EditorUI::DrawReplayEditor(Scene& scene) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float windowHeight = 140.0f * m_UIScale;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - windowHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, windowHeight));

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoTitleBar;
    
    // Premium dark-glass aesthetic
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.1f, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * m_UIScale, 15.0f * m_UIScale));

    if (ImGui::Begin("Replay Timeline", nullptr, windowFlags)) {
        
        // --- Header Section ---
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "REPLAY SYSTEM");
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Frame %d of %d", m_CurrentReplayFrame + 1, m_MaxReplayFrames);
        ImGui::EndGroup();

        ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f * m_UIScale);
        if (ImGui::Button("STOP REPLAY", ImVec2(120.0f * m_UIScale, 0))) {
            m_IsReplaying = false;
            m_ReplayPlaying = false;
        }

        ImGui::SameLine();
        ImGui::Checkbox("Free Roam", &m_ReplayFreeRoam);

        ImGui::Separator();
        ImGui::Spacing();

        // --- Controls Section ---
        float buttonSize = 40.0f * m_UIScale;
        float spacing = 8.0f * m_UIScale;
        
        // Center the controls
        float controlsWidth = 4 * buttonSize + 3 * spacing + 180.0f * m_UIScale;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - controlsWidth) * 0.5f);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 0));
        
        if (ImGui::Button("|<", ImVec2(buttonSize, buttonSize))) {
            m_CurrentReplayFrame = 0;
            m_ReplayPlaying = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("<", ImVec2(buttonSize, buttonSize))) {
            m_CurrentReplayFrame = std::max(0, m_CurrentReplayFrame - 1);
            m_ReplayPlaying = false;
        }
        ImGui::SameLine();
        
        // Play/Pause with vibrant color
        if (m_ReplayPlaying) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button(m_ReplayPlaying ? "||" : ">", ImVec2(buttonSize, buttonSize))) {
            m_ReplayPlaying = !m_ReplayPlaying;
        }
        if (m_ReplayPlaying) ImGui::PopStyleColor();
        
        ImGui::SameLine();
        if (ImGui::Button(">", ImVec2(buttonSize, buttonSize))) {
            m_CurrentReplayFrame = std::min(m_MaxReplayFrames - 1, m_CurrentReplayFrame + 1);
            m_ReplayPlaying = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(">|", ImVec2(buttonSize, buttonSize))) {
            m_CurrentReplayFrame = m_MaxReplayFrames - 1;
            m_ReplayPlaying = false;
        }
        
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (buttonSize - 20.0f * m_UIScale) * 0.5f); // Center slider vertically
        ImGui::SetNextItemWidth(150.0f * m_UIScale);
        ImGui::SliderFloat("##PlaybackSpeed", &m_ReplayPlaybackSpeed, 0.1f, 5.0f, "Speed: %.1fx");
        
        ImGui::PopStyleVar(); // ItemSpacing

        // --- Timeline Section ---
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.0f, 0.65f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderInt("##Timeline", &m_CurrentReplayFrame, 0, m_MaxReplayFrames - 1, "")) {
            m_ReplayPlaying = false;
        }
        
        ImGui::PopStyleColor(3);
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}
