#include "UIComponents.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../core/Components.h"
#include <algorithm>

namespace UIComponents {

void Tooltip(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool PropertyVec3(const char* label, glm::vec3& vec, float speed) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.3f);
    changed |= ImGui::DragFloat3("##val", &vec.x, speed);
    ImGui::PopID();
    return changed;
}

void DrawTransformEditor(Entity entity, Scene& scene) {
    if (!scene.GetRegistry().HasComponent<TransformComponent>(entity)) return;
    auto& transform = scene.GetRegistry().GetComponent<TransformComponent>(entity);

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (PropertyVec3("Position", transform.position)) transform.UpdateMatrix();
        if (PropertyVec3("Rotation", transform.rotation)) transform.UpdateMatrix();
        if (PropertyVec3("Scale", transform.scale)) transform.UpdateMatrix();
    }
}

void DrawSpringEditor(Entity entity, Scene& scene) {
    auto& registry = scene.GetRegistry();
    if (!registry.HasComponent<SpringComponent>(entity)) return;
    auto& spring = registry.GetComponent<SpringComponent>(entity);

    if (ImGui::CollapsingHeader("Spring Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Attached to Entity", &spring.isAttachedToEntity);
        
        if (spring.isAttachedToEntity) {
            ImGui::Text("Connections: %d", (int)spring.connectedEntities.size());
            
            // OPTIMIZATION: List clipping for connection list
            if (ImGui::BeginChild("ConnectionsList", ImVec2(0, 100), true)) {
                ImGuiListClipper clipper;
                clipper.Begin((int)spring.connectedEntities.size());
                while (clipper.Step()) {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                        Entity target = spring.connectedEntities[i];
                        ImGui::PushID(i);
                        ImGui::Text("Entity %d", target);
                        ImGui::SameLine();
                        if (ImGui::Button("Remove")) {
                            spring.connectedEntities.erase(spring.connectedEntities.begin() + i);
                        }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndChild();
        } else {
            PropertyVec3("Anchor", spring.fixedAnchorPoint);
        }

        ImGui::DragFloat("Rest Length", &spring.restingLength, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Stiffness", &spring.stiffness, 1.0f, 0.0f, 1000.0f);
        ImGui::DragFloat("Damping", &spring.damping, 0.1f, 0.0f, 100.0f);
    }
}

void DrawRenderEditor(Entity entity, Scene& scene, const std::vector<std::string>& availableModels, const std::vector<std::string>& availableTextures) {
    auto& registry = scene.GetRegistry();
    if (!registry.HasComponent<RenderComponent>(entity)) return;
    auto& render = registry.GetComponent<RenderComponent>(entity);

    if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Visible", &render.visible);
        
        // Model Selection
        if (ImGui::BeginCombo("Model", render.geometryName.c_str())) {
            for (const auto& model : availableModels) {
                if (ImGui::Selectable(model.c_str(), render.geometryName == model)) {
                    // Logic to change geometry would go here or be handled by a request system
                }
            }
            ImGui::EndCombo();
        }

        // Texture Selection
        if (ImGui::BeginCombo("Texture", render.texturePath.c_str())) {
            for (const auto& tex : availableTextures) {
                if (ImGui::Selectable(tex.c_str(), render.texturePath == tex)) {
                    render.texturePath = tex;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SliderInt("Shading Mode", &render.shadingMode, 0, 4);
        ImGui::SliderFloat("Opacity", &render.opacity, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Casts Shadow", &render.castsShadow);
        ImGui::Checkbox("Receive Shadows", &render.receiveShadows);
    }
}

} // namespace
