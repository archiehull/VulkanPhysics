#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "VulkanBuffer.h"
#include <stdexcept>
#include <cstring>

VulkanBuffer::VulkanBuffer(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg)
    : device(deviceArg), physicalDevice(physicalDeviceArg) {
}

VulkanBuffer::~VulkanBuffer() {
    try {
        Cleanup();
    }
    catch (...) {
    }
}

void VulkanBuffer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    allocInfo.memoryTypeIndex = VulkanUtils::FindMemoryType(
        physicalDevice,
        memRequirements.memoryTypeBits,
        properties
    );

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void VulkanBuffer::CopyData(const void* data, VkDeviceSize size) const {
    void* mappedData;
    vkMapMemory(device, bufferMemory, 0, size, 0, &mappedData);

    memcpy(mappedData, data, static_cast<size_t>(size));

    vkUnmapMemory(device, bufferMemory);
}

void VulkanBuffer::Cleanup() {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (bufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, bufferMemory, nullptr);
        bufferMemory = VK_NULL_HANDLE;
    }
}