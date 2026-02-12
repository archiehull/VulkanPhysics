#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include "../vulkan/VulkanUtils.h"

class Cubemap final {
public:
    Cubemap(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg, VkCommandPool commandPoolArg, VkQueue graphicsQueueArg);
    ~Cubemap() = default;

    Cubemap(const Cubemap&) = delete;
    Cubemap& operator=(const Cubemap&) = delete;

    void LoadFromFiles(const std::vector<std::string>& paths);

    VkDescriptorSetLayout GetDescriptorSetLayout() const { return descriptorSetLayout; }
    VkDescriptorSet GetDescriptorSet() const { return descriptorSet; }

    VkImageView GetImageView() const { return imageView; }
    VkSampler   GetSampler() const { return sampler; }

    void Cleanup();

private:
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSet();
};