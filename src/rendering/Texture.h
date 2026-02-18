#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <glm/glm.hpp>
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

    // --- Procedural Generation Methods ---

    // Generates a 1x1 solid color texture
    void GenerateSolidColor(const glm::vec4& color);

    // Generates a checkerboard pattern
    void GenerateCheckerboard(int width, int height, const glm::vec4& color1, const glm::vec4& color2, int cellSize);

    // Generates a linear gradient (vertical or horizontal)
    void GenerateGradient(int width, int height, const glm::vec4& startColor, const glm::vec4& endColor, bool isVertical);


    VkImageView GetImageView() const { return imageView; }
    VkSampler GetSampler() const { return sampler; }
    VkImage GetImage() const { return image; }

    void Cleanup() const;

private:
    // Centralized function to upload pixel data to GPU
    void CreateFromPixels(const unsigned char* pixels, int width, int height);

    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;

    mutable VkImage image = VK_NULL_HANDLE;
    mutable VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    mutable VkImageView imageView = VK_NULL_HANDLE;
    mutable VkSampler sampler = VK_NULL_HANDLE;
};