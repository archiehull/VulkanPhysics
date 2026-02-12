#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;

    // Return a const reference to a single static binding description to avoid returning by value/copy.
    static const VkVertexInputBindingDescription& getBindingDescription() {
        static const VkVertexInputBindingDescription bindingDescription = []() {
            VkVertexInputBindingDescription bd{};
            bd.binding = 0;
            bd.stride = sizeof(Vertex);
            bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return bd;
            }();
        return bindingDescription;
    }

    // Return a const reference to a single static attribute description array to avoid returning by value/copy.
    static const std::array<VkVertexInputAttributeDescription, 4>& getAttributeDescriptions() {
        static const std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions = []() {
            std::array<VkVertexInputAttributeDescription, 4> attrs{};
            attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) };
            attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) };
            attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) };
            attrs[3] = { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) };
            return attrs;
            }();
        return attributeDescriptions;
    }
};