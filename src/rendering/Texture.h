#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include "../vulkan/VulkanUtils.h"
#include <utility>

class Texture final {
public:
    Texture(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg, VkCommandPool commandPoolArg, VkQueue graphicsQueueArg);
    ~Texture() noexcept;

    // Non-copyable
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Movable
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // Loads file via stb_image, uploads to GPU, creates image view + sampler.
    bool LoadFromFile(const std::string& filepath);

    VkImageView GetImageView() const { return imageView; }
    VkSampler GetSampler() const { return sampler; }
    VkImage GetImage() const { return image; }


    void Cleanup() const;

private:
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;

    mutable VkImage image = VK_NULL_HANDLE;
    mutable VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    mutable VkImageView imageView = VK_NULL_HANDLE;
    mutable VkSampler sampler = VK_NULL_HANDLE;
};