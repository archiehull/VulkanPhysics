#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>
#include "Cubemap.h"
#include "GraphicsPipeline.h"
#include "Scene.h"

class SkyboxPass final {
public:
    SkyboxPass(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg, VkCommandPool commandPoolArg, VkQueue graphicsQueueArg);
    ~SkyboxPass();

    // Non-copyable (explicitly declared)
    SkyboxPass(const SkyboxPass&) = delete;
    SkyboxPass& operator=(const SkyboxPass&) = delete;

    void Initialize(VkRenderPass renderPass, const VkExtent2D& extent, VkDescriptorSetLayout globalSetLayout);
    void Draw(VkCommandBuffer cmd, const Scene& scene, uint32_t currentFrame, VkDescriptorSet globalDescriptorSet) const;
    void Cleanup();

    Cubemap* GetCubemap() const { return cubemap.get(); }

private:
    // Vulkan handles grouped together
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;

    // Pipeline then cubemap for improved layout (matches analyzer guidance)
    std::unique_ptr<GraphicsPipeline> pipeline;
    std::unique_ptr<Cubemap> cubemap;

    std::vector<std::string> GetSkyboxFaces(const std::string& name) const;
};