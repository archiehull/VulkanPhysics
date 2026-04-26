#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "../core/ECS.h"
#include "../rendering/Scene.h"

namespace UIComponents {
    // Shared widgets
    void Tooltip(const char* text);
    bool PropertyColor(const char* label, glm::vec4& color);
    bool PropertyVec3(const char* label, glm::vec3& vec, float speed = 0.1f);
    
    // Component editors
    void DrawTransformEditor(Entity entity, Scene& scene);
    void DrawPhysicsEditor(Entity entity, Scene& scene);
    void DrawSpringEditor(Entity entity, Scene& scene);
    void DrawRenderEditor(Entity entity, Scene& scene, const std::vector<std::string>& availableModels, const std::vector<std::string>& availableTextures);
    void DrawLightEditor(Entity entity, Scene& scene);
    void DrawOrbitEditor(Entity entity, Scene& scene);
    void DrawSpawnerEditor(Entity entity, Scene& scene, const std::vector<std::string>& availableModels, const std::vector<std::string>& availableTextures);
}
