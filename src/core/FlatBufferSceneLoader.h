#pragma once
#include <string>
#include <unordered_map>

class Scene;
struct AppConfig;

class FlatBufferSceneLoader {
public:
    static bool LoadScene(Scene& scene, AppConfig& config, const std::string& filepath);

    // Optional verbose console output when loading .bin FlatBuffer scenes.
    // Call this before LoadScene to get extra diagnostics.
    static void SetVerbose(bool v);
};
