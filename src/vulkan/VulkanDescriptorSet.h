#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VulkanDescriptorSet final {
public:
    explicit VulkanDescriptorSet(VkDevice device);
    ~VulkanDescriptorSet() = default;

    VulkanDescriptorSet(const VulkanDescriptorSet&) = delete;
    VulkanDescriptorSet& operator=(const VulkanDescriptorSet&) = delete;

    void CreateDescriptorSetLayout();
    void CreateDescriptorPool(uint32_t maxSets);
    void CreateDescriptorSets(const std::vector<VkBuffer>& uniformBuffers, VkDeviceSize bufferSize,
        VkImageView shadowImageView, VkSampler shadowSampler,
        VkImageView skyboxImageView, VkSampler skyboxSampler);
    void Cleanup();

    VkDescriptorSetLayout GetLayout() const { return descriptorSetLayout; }
    VkDescriptorSet GetDescriptorSet(uint32_t index) const { return descriptorSets[index]; }
    const std::vector<VkDescriptorSet>& GetDescriptorSets() const { return descriptorSets; }

private:
    std::vector<VkDescriptorSet> descriptorSets;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDevice device;
};