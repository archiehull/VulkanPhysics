#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class VulkanCommandBuffer final {
public:
    VulkanCommandBuffer(VkDevice device, VkPhysicalDevice physicalDevice);
    ~VulkanCommandBuffer() = default;

    VulkanCommandBuffer(const VulkanCommandBuffer&) = delete;
    VulkanCommandBuffer& operator=(const VulkanCommandBuffer&) = delete;

    void CreateCommandPool(uint32_t queueFamilyIndex);
    void CreateCommandBuffers(size_t count);

    void RecordCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
        VkRenderPass renderPass, const VkExtent2D& extent,
        VkPipeline pipeline, VkPipelineLayout pipelineLayout) const;

    void RecordOffScreenCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
        VkRenderPass renderPass, const VkExtent2D& extent,
        VkPipeline pipeline, VkPipelineLayout pipelineLayout) const;

    void Cleanup();

    VkCommandPool GetCommandPool() const { return commandPool; }
    const std::vector<VkCommandBuffer>& GetCommandBuffers() const noexcept { return commandBuffers; }
    VkCommandBuffer GetCommandBuffer(size_t index) const { return commandBuffers[index]; }

    // Single time command helpers
    VkCommandBuffer BeginSingleTimeCommands() const;
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer, VkQueue queue) const;

private:
    std::vector<VkCommandBuffer> commandBuffers;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    void RecordCommandBufferInternal(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
        VkRenderPass renderPass, const VkExtent2D& extent,
        VkPipeline pipeline, const VkClearValue& clearColor) const;
};