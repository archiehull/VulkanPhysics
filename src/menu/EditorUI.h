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

    enum class ProcTexType { SOLID, CHECKERBOARD, GRADIENT_VERT, GRADIENT_HORIZ };

    struct ProceduralTextureRequest {
        std::string name;
        ProcTexType type;
        glm::vec4 color1;
        glm::vec4 color2;
        int cellSize;
    };

    std::vector<ProceduralTextureRequest> ConsumeTextureRequests() {
        auto reqs = m_TextureRequests;
        m_TextureRequests.clear();
        return reqs;
    }

    // --- GEOMETRY CHANGE SYSTEM ---
    struct GeometryChangeRequest {
        Entity entity;
        std::string type;
        std::string path;
    };

    void RefreshModelList();
    std::vector<GeometryChangeRequest> ConsumeGeometryRequests() {
        auto reqs = m_GeometryRequests;
        m_GeometryRequests.clear();
        return reqs;
    }

    void RefreshTextureList();

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

    void SetPerformanceSettings(bool vsyncEnabled, int maxFps) {
        m_VSyncEnabled = vsyncEnabled;
        m_FpsCapEnabled = (maxFps > 0);
        if (maxFps > 0) {
            m_MaxFps = maxFps;
        }
        else if (m_MaxFps <= 0) {
            m_MaxFps = 144;
        }
    }

    bool ConsumePerformanceSettingsRequest(bool& vsyncEnabled, int& maxFps) {
        if (!m_PerformanceSettingsChanged) {
            return false;
        }

        m_PerformanceSettingsChanged = false;
        vsyncEnabled = m_VSyncEnabled;
        maxFps = m_FpsCapEnabled ? ((m_MaxFps <= 0) ? 1 : m_MaxFps) : 0;
        return true;
    }

    // Physics settings
    void SetPhysicsSettings(bool linearDampingEnabled, float linearDampingFactor, bool quadraticDragEnabled, float quadraticDragCoeff) {
        m_LinearDampingEnabled = linearDampingEnabled;
        m_LinearDampingFactor = linearDampingFactor;
        m_QuadraticDragEnabled = quadraticDragEnabled;
        m_QuadraticDragCoeff = quadraticDragCoeff;
    }

    bool ConsumePhysicsSettingsRequest(bool& linearDampingEnabled, float& linearDampingFactor, bool& quadraticDragEnabled, float& quadraticDragCoeff) {
        if (!m_PhysicsSettingsChanged) {
            return false;
        }

        m_PhysicsSettingsChanged = false;
        linearDampingEnabled = m_LinearDampingEnabled;
        linearDampingFactor = m_LinearDampingFactor;
        quadraticDragEnabled = m_QuadraticDragEnabled;
        quadraticDragCoeff = m_QuadraticDragCoeff;
        return true;
    }

    bool ConsumeSpringVisualizationRequest(bool& enabled) {
        if (!m_SpringVisualizationChanged) {
            return false;
        }

        m_SpringVisualizationChanged = false;
        enabled = m_ShowSpringVisuals;
        return true;
    }

private:
    void DrawMainMenuSection(float deltaTime, float currentTemp, const std::string& seasonName, Scene& scene, Entity activeOrbitTarget, std::string& sceneToLoad, Entity& entityToDelete);
    void DrawLoadSceneMenu(std::string& sceneToLoad);
    void DrawObjectsMenu(Scene& scene, Entity activeOrbitTarget, Entity& entityToDelete);
    void DrawMainMenuStatusBar(float deltaTime);
    void DrawPostMainMenuSection(Scene& scene, Entity entityToDelete);
    void DrawPropertyWindowsSection(Scene& scene, Entity& entityToDelete);

    Entity m_ViewRequested = MAX_ENTITIES;

    std::vector<std::string> availableCameras;
    std::string requestedCamera = "";

    std::vector<std::string> m_AvailableModels;
    std::vector<GeometryChangeRequest> m_GeometryRequests;

    std::vector<std::string> m_AvailableTextures;
    std::vector<ProceduralTextureRequest> m_TextureRequests;
    std::vector<SceneOption> m_SceneOptions;

    std::vector<std::pair<std::string, std::string>> m_DisplayBindings;

    bool m_ShowControlsWindow = false;
    bool m_ShowEntityPropertiesWindow = false;

    struct PropertyWindowData {
        int id;
        Entity selectedEntity;
        bool showList;
        bool isOpen;
        bool isPopout = false;
    };
    std::vector<PropertyWindowData> m_PropertyWindows;
    int m_NextPropertyWindowId = 1;

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

    bool m_VSyncEnabled = true;
    bool m_FpsCapEnabled = true;
    int m_MaxFps = 144;
    bool m_PerformanceSettingsChanged = false;

    // Physics settings
    bool m_LinearDampingEnabled = true;
    float m_LinearDampingFactor = 0.98f;
    bool m_QuadraticDragEnabled = true;
    float m_QuadraticDragCoeff = 0.01f;
    bool m_PhysicsSettingsChanged = false;

    bool m_ShowSpringVisuals = false;
    bool m_SpringVisualizationChanged = false;

};