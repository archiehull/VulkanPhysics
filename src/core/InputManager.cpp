#include "InputManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// Define string-to-key mappings
std::unordered_map<std::string, int> InputManager::StringToKeyMap = {
    {"W", GLFW_KEY_W}, {"A", GLFW_KEY_A}, {"S", GLFW_KEY_S}, {"D", GLFW_KEY_D},
    {"Q", GLFW_KEY_Q}, {"E", GLFW_KEY_E}, {"R", GLFW_KEY_R}, {"T", GLFW_KEY_T},
    {"Y", GLFW_KEY_Y}, {"U", GLFW_KEY_U}, {"I", GLFW_KEY_I}, {"O", GLFW_KEY_O},
    {"P", GLFW_KEY_P},
    {"UP", GLFW_KEY_UP}, {"DOWN", GLFW_KEY_DOWN}, {"LEFT", GLFW_KEY_LEFT}, {"RIGHT", GLFW_KEY_RIGHT},
    {"PAGE_UP", GLFW_KEY_PAGE_UP}, {"PAGE_DOWN", GLFW_KEY_PAGE_DOWN},
    {"LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT}, {"RIGHT_SHIFT", GLFW_KEY_RIGHT_SHIFT},
    {"LEFT_CONTROL", GLFW_KEY_LEFT_CONTROL}, {"RIGHT_CONTROL", GLFW_KEY_RIGHT_CONTROL},
    {"SPACE", GLFW_KEY_SPACE}, {"ESCAPE", GLFW_KEY_ESCAPE},
    {"F1", GLFW_KEY_F1}, {"F2", GLFW_KEY_F2}, {"F3", GLFW_KEY_F3}, {"F4", GLFW_KEY_F4},
    {"F5", GLFW_KEY_F5}, {"F6", GLFW_KEY_F6}, {"F7", GLFW_KEY_F7}, {"F8", GLFW_KEY_F8}
};

// Define string-to-action mappings
std::unordered_map<std::string, InputAction> InputManager::StringToActionMap = {
    {"MoveForward", InputAction::MoveForward},
    {"MoveBackward", InputAction::MoveBackward},
    {"MoveLeft", InputAction::MoveLeft},
    {"MoveRight", InputAction::MoveRight},
    {"MoveUp", InputAction::MoveUp},
    {"MoveDown", InputAction::MoveDown},
    {"Sprint", InputAction::Sprint},
    {"LookUp", InputAction::LookUp},
    {"LookDown", InputAction::LookDown},
    {"LookLeft", InputAction::LookLeft},
    {"LookRight", InputAction::LookRight},
    {"Camera1", InputAction::Camera1},
    {"Camera2", InputAction::Camera2},
    {"Camera3", InputAction::Camera3},
    {"Camera4", InputAction::Camera4},
    {"Camera5", InputAction::Camera5},
    {"Camera6", InputAction::Camera6},
    {"Camera7", InputAction::Camera7},
    {"Camera8", InputAction::Camera8},
    {"IgniteTarget", InputAction::IgniteTarget},
    {"ResetEnvironment", InputAction::ResetEnvironment},
    {"TimeSpeedUp", InputAction::TimeSpeedUp},
    {"ToggleShading", InputAction::ToggleShading},
    {"ToggleShadows", InputAction::ToggleShadows},
    {"NextSeason", InputAction::NextSeason},
    {"ToggleWeather", InputAction::ToggleWeather},
    {"SpawnDustCloud", InputAction::SpawnDustCloud},
    {"PauseToggle", InputAction::PauseToggle},
    {"Exit", InputAction::Exit}
};

std::string InputManager::Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::unordered_map<std::string, std::string> InputManager::GetDefaultBindings() {
    return {
        // Movement (WASD)
        {"MoveForward", "W"},
        {"MoveBackward", "S"},
        {"MoveLeft", "A"},
        {"MoveRight", "D"},
        {"MoveUp", "E,PAGE_UP"},
        {"MoveDown", "Q,PAGE_DOWN"},
        {"Sprint", "LEFT_SHIFT,RIGHT_SHIFT"},

        // Look (Arrows)
        {"LookUp", "UP"},
        {"LookDown", "DOWN"},
        {"LookLeft", "LEFT"},
        {"LookRight", "RIGHT"},

        // Cameras
        {"Camera1", "F1"},
        {"Camera2", "F2"},
        {"Camera3", "F3"},
        {"Camera4", "F4"}, // Adjust default keys as preferred
        {"Camera5", "F5"},
        {"Camera6", "F6"},
        {"Camera7", "F7"},
        {"Camera8", "F8"},

        // Environment & Actions
        {"IgniteTarget", "F4"},
        {"ResetEnvironment", "R"},
        {"TimeSpeedUp", "T"},
        {"ToggleShading", "Y"},
        {"ToggleShadows", "U"},
        {"NextSeason", "I"},
        {"ToggleWeather", "O"},
        {"SpawnDustCloud", "P"},
        {"PauseToggle", "SPACE"},
        {"Exit", "ESCAPE"}
    };
}


void InputManager::ClearBindings() {
    keyBindings.clear();
}

std::unordered_map<std::string, std::string> InputManager::LoadFromBindings(const std::unordered_map<std::string, std::string>& overrides) {
    ClearBindings();

    // 1. Start with standard engine defaults
    std::unordered_map<std::string, std::string> activeBindings = GetDefaultBindings();

    // 2. Overwrite defaults with any scene-specific config passed in
    for (const auto& pair : overrides) {
        activeBindings[pair.first] = pair.second;
    }

    // 3. Translate the final active map into actual integer Keybinds
    for (const auto& pair : activeBindings) {
        const std::string& actionStr = pair.first;
        const std::string& keysStr = pair.second;

        auto actionIt = StringToActionMap.find(actionStr);
        if (actionIt == StringToActionMap.end()) {
            std::cerr << "Warning: Unknown input action '" << actionStr << "'" << std::endl;
            continue;
        }
        InputAction action = actionIt->second;

        std::stringstream ss(keysStr);
        std::string keyStr;
        while (std::getline(ss, keyStr, ',')) {
            keyStr = Trim(keyStr);
            auto keyIt = StringToKeyMap.find(keyStr);
            if (keyIt != StringToKeyMap.end()) {
                BindKey(keyIt->second, action);
            }
            else {
                std::cerr << "Warning: Unknown key '" << keyStr << "' mapped to '" << actionStr << "'" << std::endl;
            }
        }
    }

    // 4. Return the fully resolved map so the UI knows what is actually active
    return activeBindings;
}

void InputManager::BindKey(int key, InputAction action) {
    keyBindings[key] = action;
}

void InputManager::UnbindKey(int key) {
    keyBindings.erase(key);
}

bool InputManager::IsActionHeld(InputAction action) const {
    auto it = currentStates.find(action);
    return it != currentStates.end() ? it->second : false;
}

bool InputManager::IsActionJustPressed(InputAction action) const {
    bool current = IsActionHeld(action);
    auto it = previousStates.find(action);
    bool previous = it != previousStates.end() ? it->second : false;
    return current && !previous;
}

bool InputManager::IsActionJustReleased(InputAction action) const {
    bool current = IsActionHeld(action);
    auto it = previousStates.find(action);
    bool previous = it != previousStates.end() ? it->second : false;
    return !current && previous;
}

void InputManager::Update() {
    previousStates = currentStates;
}

void InputManager::HandleKeyEvent(int key, int action) {
    auto it = keyBindings.find(key);
    if (it != keyBindings.end()) {
        if (action == GLFW_PRESS) currentStates[it->second] = true;
        else if (action == GLFW_RELEASE) currentStates[it->second] = false;
    }
}