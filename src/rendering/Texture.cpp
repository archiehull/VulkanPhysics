#include "Texture.h"
#include "../vulkan/VulkanBuffer.h"
#include "../vulkan/VulkanUtils.h"
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <iostream>
#include <utility>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

Texture::Texture(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg, VkCommandPool commandPoolArg, VkQueue graphicsQueueArg)
    : device(deviceArg), physicalDevice(physicalDeviceArg), commandPool(commandPoolArg), graphicsQueue(graphicsQueueArg) {
}

Texture::~Texture() noexcept {
    try {
        Cleanup();
    }
    catch (...) {
        // Ensure destructor does not allow exceptions to propagate.
    }
}

Texture::Texture(Texture&& other) noexcept
    : device(other.device),
    physicalDevice(other.physicalDevice),
    commandPool(other.commandPool),
    graphicsQueue(other.graphicsQueue),
    image(std::exchange(other.image, VK_NULL_HANDLE)),
    imageMemory(std::exchange(other.imageMemory, VK_NULL_HANDLE)),
    imageView(std::exchange(other.imageView, VK_NULL_HANDLE)),
    sampler(std::exchange(other.sampler, VK_NULL_HANDLE)) {
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this == &other) return *this;

    // Clean up current resources
    Cleanup();

    device = other.device;
    physicalDevice = other.physicalDevice;
    commandPool = other.commandPool;
    graphicsQueue = other.graphicsQueue;

    image = std::exchange(other.image, VK_NULL_HANDLE);
    imageMemory = std::exchange(other.imageMemory, VK_NULL_HANDLE);
    imageView = std::exchange(other.imageView, VK_NULL_HANDLE);
    sampler = std::exchange(other.sampler, VK_NULL_HANDLE);

    return *this;
}

bool Texture::LoadFromFile(const std::string& filepath) {
    int texWidth = 0, texHeight = 0, texChannels = 0;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    bool usedStbLoaded = true;
    bool manualAlloc = false;

    if (!pixels) {
        const std::string defaultTexturePath = "textures/default.png";
        std::cerr << "Warning: Failed to load texture '" << filepath << "'. Attempting default texture '" << defaultTexturePath << "'.\n";
        pixels = stbi_load(defaultTexturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) {
            std::cerr << "Warning: Failed to load default texture '" << defaultTexturePath << "'. Falling back to 1x1 white pixel.\n";
            // create a 1x1 white pixel so we always return a valid texture
            texWidth = 1;
            texHeight = 1;
            texChannels = 4;
            pixels = new stbi_uc[4]{ 255, 255, 255, 255 };
            usedStbLoaded = false;
            manualAlloc = true;
        }
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

    // 1. Create Staging Buffer
    VulkanBuffer stagingBuffer(device, physicalDevice);
    stagingBuffer.CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    stagingBuffer.CopyData(pixels, imageSize);

    if (usedStbLoaded) {
        stbi_image_free(pixels);
        pixels = nullptr;
    }
    else if (manualAlloc && pixels) {
        delete[] pixels;
        pixels = nullptr;
    }

    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    // 2. Create Image (Using VulkanUtils)
    VulkanUtils::CreateImage(
        device, physicalDevice,
        static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight),
        1, 1,
        format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        image, imageMemory
    );

    // 3. Transition -> Copy -> Transition (Using VulkanUtils)
    VulkanUtils::TransitionImageLayout(
        device, commandPool, graphicsQueue,
        image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    VulkanUtils::CopyBufferToImage(
        device, commandPool, graphicsQueue,
        stagingBuffer.GetBuffer(), image,
        static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight)
    );

    VulkanUtils::TransitionImageLayout(
        device, commandPool, graphicsQueue,
        image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    // 4. Create Image View (Using VulkanUtils)
    imageView = VulkanUtils::CreateImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT);

    // 5. Create Sampler 
    // (This logic is specific to texture sampling params like anisotropy, so we keep it here)
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    VkPhysicalDeviceFeatures deviceFeatures{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    // Enable anisotropy if available
    samplerInfo.anisotropyEnable = (deviceFeatures.samplerAnisotropy != 0) ? VK_TRUE : VK_FALSE;
    if (samplerInfo.anisotropyEnable == VK_TRUE) {
        const float maxAniso = std::min<float>(properties.limits.maxSamplerAnisotropy, 16.0f);
        samplerInfo.maxAnisotropy = maxAniso;
    }
    else {
        samplerInfo.maxAnisotropy = 1.0f;
    }

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler!");
    }

    return true;
}

void Texture::Cleanup() const {
    VulkanUtils::CleanupImageResources(device, image, imageMemory, imageView, sampler);
}