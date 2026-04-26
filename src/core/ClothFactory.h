#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "../core/ECS.h"
#include "../rendering/Scene.h"

class ClothFactory {
public:
    static Entity CreateClothGrid(Scene& scene, VkDevice device, VkPhysicalDevice physicalDevice, const glm::vec3& position, int width, int height, float spacing, float mass, float stiffness, float damping, const std::string& texturePath);
};
