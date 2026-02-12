#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <optional>
#include <vulkan/vulkan_core.h>

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

class VulkanDevice final {
public:
    VulkanDevice(VkInstance instanceArg, VkSurfaceKHR surfaceArg);
    ~VulkanDevice() = default;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    VulkanDevice(VulkanDevice&&) = default;
    VulkanDevice& operator=(VulkanDevice&&) = default;

    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void Cleanup();

    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
    VkDevice GetDevice() const { return device; }
    VkQueue GetGraphicsQueue() const { return graphicsQueue; }
    VkQueue GetPresentQueue() const { return presentQueue; }
    const QueueFamilyIndices& GetQueueFamilies() const { return cachedQueueFamilies; }

private:
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    QueueFamilyIndices cachedQueueFamilies;

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physDevice) const;
    bool isDeviceSuitable(VkPhysicalDevice physDevice) const;
    bool checkDeviceExtensionSupport(VkPhysicalDevice physDevice) const;
};