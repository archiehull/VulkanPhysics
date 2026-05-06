#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "VulkanBuffer.h"
#include <stdexcept>
#include <cstring>
#include <atomic>
#include <iostream>

static std::atomic<int> g_ActiveBufferCount{ 0 };

VulkanBuffer::VulkanBuffer(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg)
    : device(deviceArg), physicalDevice(physicalDeviceArg) {
    g_ActiveBufferCount++;
}

VulkanBuffer::~VulkanBuffer() {
    try {
        Cleanup();
    }
    catch (...) {
    }
    g_ActiveBufferCount--;
}

void VulkanBuffer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties) {
    
    if (size == 0) {
        std::cerr << "[VulkanBuffer] WARNING: CreateBuffer called with size 0! Usage: " << usage << std::endl;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = (size == 0) ? 1 : size; // Fallback to 1 byte
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
        std::cerr << "[VulkanBuffer] CRITICAL: vkAllocateMemory FAILED!" << std::endl;
        std::cerr << "  Requested Size: " << size << " bytes" << std::endl;
        std::cerr << "  Actual Alloc Size: " << allocInfo.allocationSize << " bytes" << std::endl;
        std::cerr << "  Memory Type Index: " << allocInfo.memoryTypeIndex << std::endl;
        std::cerr << "  Usage Flags: " << usage << std::endl;
        std::cerr << "  Active Buffers: " << g_ActiveBufferCount.load() << std::endl;
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    if (g_ActiveBufferCount.load() % 100 == 0 || size > 1024 * 1024) {
         std::cout << "[VulkanBuffer] Allocated " << size << " bytes (Usage: " << usage << ", Total Active: " << g_ActiveBufferCount.load() << ")" << std::endl;
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