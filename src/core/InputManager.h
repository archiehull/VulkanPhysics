#pragma once

#include <GLFW/glfw3.h>
#include <unordered_map>
#include <string>
#include <vector>

enum class InputAction {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Sprint,

    LookUp,
    LookDown,
    LookLeft,
    LookRight,

    CameraOutside,
    CameraFreeRoam,
    CameraCacti,
    CameraCustom1,
    CameraCustom2,
    CameraCustom3,
    CameraCustom4,

    IgniteTarget,
    SpawnDustCloud,

    TimeSpeedUp,
    ToggleShading,
    ToggleShadows,
    NextSeason,
    ToggleWeather,
    ResetEnvironment,
    PauseToggle,
    Exit
};

class InputManager {
public:
    InputManager() = default;
    ~InputManager() = default;

    // Load bindings from a text file
    std::unordered_map<std::string, std::string> LoadFromBindings(const std::unordered_map<std::string, std::string>& overrides);
    void ClearBindings();

    void BindKey(int key, InputAction action);
    void UnbindKey(int key);

    bool IsActionHeld(InputAction action) const;
    bool IsActionJustPressed(InputAction action) const;
    bool IsActionJustReleased(InputAction action) const;

    void Update();
    void HandleKeyEvent(int key, int action);

private:
    std::unordered_map<int, InputAction> keyBindings;
    std::unordered_map<InputAction, bool> currentStates;
    std::unordered_map<InputAction, bool> previousStates;

    // Helpers to translate string names from the config file into C++ enums/macros
    static std::unordered_map<std::string, int> StringToKeyMap;
    static std::unordered_map<std::string, InputAction> StringToActionMap;

    static std::unordered_map<std::string, std::string> GetDefaultBindings();

    // Helper to trim whitespace from strings
    std::string Trim(const std::string& str);
};