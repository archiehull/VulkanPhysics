#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <glm/glm.hpp>
#include "../rendering/GraphicsPipeline.h"
#include "../vulkan/VulkanDevice.h"
#include "../vulkan/VulkanUtils.h"

class ShadowPass final {
public:
    ShadowPass(VulkanDevice* device, uint32_t width, uint32_t height);
    ~ShadowPass() = default;

    ShadowPass(const ShadowPass&) = delete;
    ShadowPass& operator=(const ShadowPass&) = delete;

    void Initialize(VkDescriptorSetLayout globalSetLayout);
    void Cleanup();

    void Begin(VkCommandBuffer cmd) const;
    void End(VkCommandBuffer cmd) const { vkCmdEndRenderPass(cmd); }

    VkImageView GetShadowImageView() const { return shadowImageView; }
    VkSampler GetShadowSampler() const { return shadowSampler; }
    VkRenderPass GetRenderPass() const { return renderPass; }
    GraphicsPipeline* GetPipeline() const { return pipeline.get(); }
    const VkExtent2D& GetExtent() const { return extent; }

private:
    VulkanDevice* device;
    std::unique_ptr<GraphicsPipeline> pipeline;
    VkImage shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowImageMemory = VK_NULL_HANDLE;
    VkImageView shadowImageView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkExtent2D extent;

    void CreateResources();
    void CreateRenderPass();
    void CreateFramebuffer();
    void CreatePipeline(VkDescriptorSetLayout globalSetLayout);
};