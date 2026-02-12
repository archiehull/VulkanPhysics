#pragma once

#include <vulkan/vulkan.h>
#include "VulkanUtils.h"

class VulkanBuffer final {
public:
    VulkanBuffer(VkDevice device, VkPhysicalDevice physicalDevice);
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    VulkanBuffer(VulkanBuffer&&) = default;
    VulkanBuffer& operator=(VulkanBuffer&&) = default;

    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties);

    void CopyData(const void* data, VkDeviceSize size) const;

    VkBuffer GetBuffer() const { return buffer; }
    VkDeviceMemory GetBufferMemory() const { return bufferMemory; }

    void Cleanup();

private:
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
};