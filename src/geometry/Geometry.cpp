#include "Geometry.h"
#include <stdexcept>

Geometry::Geometry(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg)
    : device(deviceArg), physicalDevice(physicalDeviceArg) {
}

void Geometry::CreateBuffers() {
    if (vertices.empty()) {
        throw std::runtime_error("No vertices to create buffer from!");
    }

    // Create vertex buffer
    vertexBuffer = std::make_unique<VulkanBuffer>(device, physicalDevice);
    const VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();

    vertexBuffer->CreateBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    vertexBuffer->CopyData(vertices.data(), vertexBufferSize);

    // Create index buffer if indices exist
    if (!indices.empty()) {
        indexBuffer = std::make_unique<VulkanBuffer>(device, physicalDevice);
        const VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();

        indexBuffer->CreateBuffer(
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        indexBuffer->CopyData(indices.data(), indexBufferSize);
    }
}

void Geometry::Bind(VkCommandBuffer commandBuffer) const {
    const VkBuffer vb = vertexBuffer->GetBuffer();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, &offset);

    if (HasIndices()) {
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
    }
}

void Geometry::Draw(VkCommandBuffer commandBuffer) const {
    if (HasIndices()) {
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }
    else {
        vkCmdDraw(commandBuffer, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }
}

void Geometry::Cleanup() {
    if (vertexBuffer) {
        vertexBuffer->Cleanup();
    }
    if (indexBuffer) {
        indexBuffer->Cleanup();
    }
}