#pragma once

#include <string>
#include <vector>
#include "../core/Config.h"

class EditorUI {
public:
    EditorUI() = default;
    ~EditorUI() = default;

    // Scans filesystem and sets the default selection
    void Initialize(const std::string& configPath, const std::string& defaultSceneName = "init");

    // Renders the top menu bar. Returns a path if a new scene is selected.
    std::string Draw(float deltaTime, float currentTemp, const std::string& seasonName);

    // Returns the path determined during Initialize
    std::string GetInitialScenePath() const;

private:
    std::vector<SceneOption> m_SceneOptions;
    int m_SelectedSceneIndex = 0;
    std::string m_ConfigRoot;

    bool m_ShowDemoWindow = false;
};