#pragma once

#include <string>
#include <vector>
#include "../core/Config.h"
#include "../rendering/Scene.h"
#include <chrono>

struct NetworkTelemetry {
    int localPeerId = -1;
    int connectedPeers = 0;
    uint16_t localPort = 0;
    bool isRunning = false;
};

struct UIProfiler {
    float drawMainMenuTime = 0.0f;
    float drawWindowsTime = 0.0f;
    float totalTime = 0.0f;
    float updateTime = 0.0f; // ms spent in update (physics + logic)
    float physicsTime = 0.0f; // ms spent in physics
    float renderTime = 0.0f;  // ms spent in render/draw
    int threadCount = 0;      // active threads
    unsigned long threadAffinityMask = 0; // affinity bitmask
    float targetRenderHz = 60.0f;
    float targetSimulationHz = 120.0f;
    float actualRenderHz = 0.0f;
    float actualSimulationHz = 0.0f;
    unsigned long renderAffinityMask = 0;
    unsigned long simulationAffinityMask = 0;
    unsigned int renderThreadId = 0;
    unsigned int simulationThreadId = 0;
    bool affinityCompliant = false;
    bool renderAffinityApplied = false;
    bool simulationAffinityApplied = false;
    bool showProfiler = false;
    NetworkTelemetry network;
};

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

    // Runtime profiling setters
    void SetUpdateTime(float ms);
    void SetPhysicsTime(float ms);
    void SetRenderTime(float ms);
    void SetThreadInfo(int count, unsigned long affinityMask);

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

    std::vector<Entity> ConsumeDespawnRequests() {
        auto reqs = m_DespawnRequests;
        m_DespawnRequests.clear();
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

    void SetRuntimeSettings(float renderHz, float simulationHz) {
        m_TargetRenderHz = renderHz;
        m_TargetSimulationHz = simulationHz;
        m_Profiler.targetRenderHz = renderHz;
        m_Profiler.targetSimulationHz = simulationHz;
    }

    bool ConsumeRuntimeSettingsRequest(float& renderHz, float& simulationHz) {
        if (!m_RuntimeSettingsChanged) {
            return false;
        }

        m_RuntimeSettingsChanged = false;
        renderHz = m_TargetRenderHz;
        simulationHz = m_TargetSimulationHz;
        return true;
    }

    void SetRuntimeTelemetry(
        float actualRenderHz,
        float actualSimulationHz,
        unsigned long renderAffinityMask,
        unsigned long simulationAffinityMask,
        unsigned int renderThreadId,
        unsigned int simulationThreadId,
        bool affinityCompliant,
        bool renderAffinityApplied,
        bool simulationAffinityApplied) {
        m_Profiler.actualRenderHz = actualRenderHz;
        m_Profiler.actualSimulationHz = actualSimulationHz;
        m_Profiler.renderAffinityMask = renderAffinityMask;
        m_Profiler.simulationAffinityMask = simulationAffinityMask;
        m_Profiler.renderThreadId = renderThreadId;
        m_Profiler.simulationThreadId = simulationThreadId;
        m_Profiler.affinityCompliant = affinityCompliant;
        m_Profiler.renderAffinityApplied = renderAffinityApplied;
        m_Profiler.simulationAffinityApplied = simulationAffinityApplied;
    }

    void SetNetworkTelemetry(const NetworkTelemetry& telemetry) {
        m_Profiler.network = telemetry;
    }

    // Physics settings
    void SetPhysicsSettings(
        bool linearDampingEnabled,
        float linearDampingFactor,
        bool quadraticDragEnabled,
        float quadraticDragCoeff,
        bool sleepNormalThresholdEnabled,
        float sleepNormalThreshold,
        bool sleepTangentialThresholdEnabled,
        float sleepTangentialThreshold) {
        m_LinearDampingEnabled = linearDampingEnabled;
        m_LinearDampingFactor = linearDampingFactor;
        m_QuadraticDragEnabled = quadraticDragEnabled;
        m_QuadraticDragCoeff = quadraticDragCoeff;
        m_SleepNormalThresholdEnabled = sleepNormalThresholdEnabled;
        m_SleepNormalThreshold = sleepNormalThreshold;
        m_SleepTangentialThresholdEnabled = sleepTangentialThresholdEnabled;
        m_SleepTangentialThreshold = sleepTangentialThreshold;
    }   

    bool ConsumePhysicsSettingsRequest(
        bool& linearDampingEnabled,
        float& linearDampingFactor,
        bool& quadraticDragEnabled,
        float& quadraticDragCoeff,
        bool& sleepNormalThresholdEnabled,
        float& sleepNormalThreshold,
        bool& sleepTangentialThresholdEnabled,
        float& sleepTangentialThreshold) {
        if (!m_PhysicsSettingsChanged) {
            return false;
        }

        m_PhysicsSettingsChanged = false;
        linearDampingEnabled = m_LinearDampingEnabled;
        linearDampingFactor = m_LinearDampingFactor;
        quadraticDragEnabled = m_QuadraticDragEnabled;
        quadraticDragCoeff = m_QuadraticDragCoeff;
        sleepNormalThresholdEnabled = m_SleepNormalThresholdEnabled;
        sleepNormalThreshold = m_SleepNormalThreshold;
        sleepTangentialThresholdEnabled = m_SleepTangentialThresholdEnabled;
        sleepTangentialThreshold = m_SleepTangentialThreshold;
        return true;
    }

    void SetColliderVisualizationMode(int mode) {
        m_ColliderVisualizationMode = mode;
    }

    bool ConsumeColliderVisualizationRequest(int& mode) {
        if (!m_ColliderVisualizationChanged) {
            return false;
        }

        m_ColliderVisualizationChanged = false;
        mode = m_ColliderVisualizationMode;
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

    bool ConsumeSpawnerVisualizationRequest(bool& enabled) {
        if (!m_SpawnerVisualizationChanged) {
            return false;
        }

        m_SpawnerVisualizationChanged = false;
        enabled = m_ShowSpawnerVisuals;
        return true;
    }
    bool ConsumeGenerateLookaheadRequest(float& outTimeframe) {
        if (m_GenerateLookaheadRequested) {
            outTimeframe = m_LookaheadTimeframe;
            m_GenerateLookaheadRequested = false;
            return true;
        }
        return false;
    }

    bool GetReplayFreeRoam() const { return m_ReplayFreeRoam; }

    bool IsReplaying() const { return m_IsReplaying; }
    void SetIsReplaying(bool isReplaying) { m_IsReplaying = isReplaying; }
    int GetReplayFrame() const { return m_CurrentReplayFrame; }
    void SetReplayFrame(int frame) { m_CurrentReplayFrame = frame; }
    void SetMaxReplayFrames(int maxFrames) { m_MaxReplayFrames = maxFrames; }

private:
    void DrawMainMenuSection(float deltaTime, float currentTemp, const std::string& seasonName, Scene& scene, Entity activeOrbitTarget, std::string& sceneToLoad, Entity& entityToDelete);
    void DrawLoadSceneMenu(std::string& sceneToLoad);
    void DrawObjectsMenu(Scene& scene, Entity activeOrbitTarget, Entity& entityToDelete);
    void DrawMainMenuStatusBar(float deltaTime);
    void DrawPostMainMenuSection(Scene& scene, Entity entityToDelete);
    void DrawPropertyWindowsSection(Scene& scene, Entity& entityToDelete);
    void DrawReplayEditor(Scene& scene);

    Entity m_ViewRequested = MAX_ENTITIES;

    std::vector<std::string> availableCameras;
    std::string requestedCamera = "";

    std::vector<std::string> m_AvailableModels;
    std::vector<GeometryChangeRequest> m_GeometryRequests;
    std::vector<Entity> m_DespawnRequests;

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

    UIProfiler m_Profiler;

    float m_UIScale = 1.0f;

    int m_SelectedSceneIndex = 0;
    std::string m_ConfigRoot;
    float m_ClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f }; // Default dark grey

    bool m_ShowDemoWindow = false;
    bool m_ShowRuntimeWindow = false;

    bool m_IsPaused = false;
    float m_TimeScale = 1.0f;
    float m_StepSize = 0.0166f; // Default to ~60fps (16.6ms)
    bool m_StepRequested = false;
    bool m_RestartRequested = false;

    bool m_VSyncEnabled = true;
    bool m_FpsCapEnabled = true;
    int m_MaxFps = 144;
    bool m_PerformanceSettingsChanged = false;

    float m_TargetRenderHz = 60.0f;
    float m_TargetSimulationHz = 120.0f;
    bool m_RuntimeSettingsChanged = false;

    // Physics settings
    bool m_LinearDampingEnabled = true;
    float m_LinearDampingFactor = 0.98f;
    bool m_QuadraticDragEnabled = true;
    float m_QuadraticDragCoeff = 0.01f;
    bool m_SleepNormalThresholdEnabled = true;
    float m_SleepNormalThreshold = 0.08f;
    bool m_SleepTangentialThresholdEnabled = true;
    float m_SleepTangentialThreshold = 0.12f;
    bool m_PhysicsSettingsChanged = false;

    int m_ColliderVisualizationMode = 0;
    bool m_ColliderVisualizationChanged = false;

    bool m_ShowSpringVisuals = false;
    bool m_SpringVisualizationChanged = false;
    bool m_ShowSpawnerVisuals = false;
    bool m_SpawnerVisualizationChanged = false;

    // Lookahead Replay UI state
    bool m_IsReplaying = false;
    int m_CurrentReplayFrame = 0;
    int m_MaxReplayFrames = 0;
    bool m_GenerateLookaheadRequested = false;
    float m_LookaheadTimeframe = 5.0f;
    bool m_ReplayFreeRoam = true;

    // Replay Playback State
    bool m_ReplayPlaying = false;
    float m_ReplayPlaybackSpeed = 1.0f;
    float m_ReplayAccumulator = 0.0f;
};
