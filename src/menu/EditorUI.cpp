#include "EditorUI.h"
#include "imgui.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include "../systems/AnimationSystem.h"

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
    std::string sceneToLoad = "";
    Entity entityToDelete = MAX_ENTITIES;

    ImGui::GetIO().FontGlobalScale = m_UIScale;

    // Draw main menu first (top bar)
    DrawMainMenuSection(deltaTime, currentTemp, seasonName, scene, activeOrbitTarget, sceneToLoad, entityToDelete);

    // Property windows may set `entityToDelete` (Delete button lives in property windows)
    // Ensure we draw property windows before the post-main-menu processing so deletion takes effect
    DrawPropertyWindowsSection(scene, entityToDelete);

    // Post main-menu section performs cleanup actions such as handling entity deletions
    DrawPostMainMenuSection(scene, entityToDelete);

    return sceneToLoad;
}

void EditorUI::ApplyReplayLookahead(Scene& scene) {
    if (m_AnimationReplayScrubEnabled && AnimationSystem::HasLookahead()) {
        AnimationSystem::ScrubLookahead(scene, m_AnimationScrubTime, m_ViewRecordedCameraInReplay);
    }
    else if (!m_AnimationReplayScrubEnabled) {
        AnimationSystem::HideReplayCameraModel(scene);
    }
}

void EditorUI::ToggleReplayPlayback() {
    if (!m_AnimationReplayScrubEnabled || !AnimationSystem::HasLookahead()) {
        return;
    }

    if (m_AnimationReplayPlaying) {
        m_AnimationReplayPlaying = false;
        return;
    }

    const float replayDuration = std::max(AnimationSystem::GetLookaheadDuration(), 0.001f);
    if (m_AnimationScrubTime >= replayDuration) {
        m_AnimationScrubTime = 0.0f;
    }

    m_AnimationReplayPlaying = true;
    m_IsPaused = true;
}
