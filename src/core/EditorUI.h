#pragma once

#include <string>
#include <vector>
#include "../core/Config.h"
#include "../rendering/Scene.h"

class EditorUI {
public:
    EditorUI() = default;
    ~EditorUI() = default;

    // Scans filesystem and sets the default selection
    void Initialize(const std::string& configPath, const std::string& defaultSceneName = "init");

    void SetInputBindings(const std::unordered_map<std::string, std::string>& bindings);

    // Renders the top menu bar. Returns a path if a new scene is selected.
    std::string Draw(float deltaTime, float currentTemp, const std::string& seasonName, Scene& scene, Entity activeOrbitTarget = MAX_ENTITIES);    // Returns the path determined during Initialize
    std::string GetInitialScenePath() const;

    const float* GetClearColor() const { return m_ClearColor; }

    Entity ConsumeViewRequest() {
        Entity req = m_ViewRequested;
        m_ViewRequested = MAX_ENTITIES;
        return req;
    }

    float GetStepSize() const { return m_StepSize; }
    bool IsPaused() const { return m_IsPaused; }
    void SetPaused(bool paused) { m_IsPaused = paused; }
    float GetTimeScale() const { return m_TimeScale; }
    void SetTimeScale(float scale) { m_TimeScale = scale; }
    float GetUIScale() const { return m_UIScale; }

    bool ConsumeStepRequest() { bool req = m_StepRequested; m_StepRequested = false; return req; }
    bool ConsumeRestartRequest() { bool req = m_RestartRequested; m_RestartRequested = false; return req; }

    void SetAvailableCameras(const std::vector<std::string>& cameras);
    std::string ConsumeCameraSwitchRequest();

private:
    Entity m_ViewRequested = MAX_ENTITIES;

    std::vector<std::string> availableCameras;
    std::string requestedCamera = "";

    std::vector<SceneOption> m_SceneOptions;

    std::vector<std::pair<std::string, std::string>> m_DisplayBindings;

    bool m_ShowControlsWindow = false;
    bool m_ShowEntityPropertiesWindow = false;

    float m_UIScale = 1.0f;

    int m_SelectedSceneIndex = 0;
    std::string m_ConfigRoot;
    float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f }; // Default dark grey

    bool m_ShowDemoWindow = false;

    bool m_IsPaused = false;
    float m_TimeScale = 1.0f;
    float m_StepSize = 0.0166f; // Default to ~60fps (16.6ms)
    bool m_StepRequested = false;
    bool m_RestartRequested = false;
};