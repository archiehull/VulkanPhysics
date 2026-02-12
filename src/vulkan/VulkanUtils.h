#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <stdexcept>

namespace VulkanUtils {
    // --- Constants ---
    extern const std::vector<const char*> validationLayers;
    extern const std::vector<const char*> deviceExtensions;
    extern const bool enableValidationLayers;

    // --- Debug & Validation Functions ---
    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger);

    void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks* pAllocator);

    bool CheckValidationLayerSupport();

    // Rule ID: OPT.33 - Changed return by value to out-parameter
    void GetRequiredExtensions(std::vector<const char*>& extensions);

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        const void* pUserData);

    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    // --- Helper Functions (The missing declarations) ---

    // Finds a memory type that satisfies the filter and property requirements
    uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Creates a VkImage and allocates/binds its memory
    void CreateImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, uint32_t mipLevels, uint32_t arrayLayers,
        VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
        VkImage& image, VkDeviceMemory& imageMemory, VkImageCreateFlags flags = 0);

    // Creates a generic VkImageView
    VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D, uint32_t layerCount = 1);

    // Creates a generic Command Buffer for single-time commands (used for transfers)
    VkCommandBuffer BeginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
    void EndSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkCommandBuffer commandBuffer);

    // Handles image layout transitions
    void TransitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue,
        VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t layerCount = 1);

    // Copies buffer data to an image
    void CopyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    // Clean up image resources
    void CleanupImageResources(VkDevice device, VkImage& image, VkDeviceMemory& imageMemory, VkImageView& imageView, VkSampler& sampler);
}