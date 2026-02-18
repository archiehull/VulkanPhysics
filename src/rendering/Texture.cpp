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

    // Call the centralized upload function
    try {
        CreateFromPixels(pixels, texWidth, texHeight);
    }
    catch (const std::exception& e) {
        std::cerr << "Error creating texture from pixels: " << e.what() << std::endl;
        if (usedStbLoaded && pixels) stbi_image_free(pixels);
        else if (manualAlloc && pixels) delete[] pixels;
        return false;
    }

    if (usedStbLoaded) {
        stbi_image_free(pixels);
    }
    else if (manualAlloc && pixels) {
        delete[] pixels;
    }

    return true;
}

void Texture::GenerateSolidColor(const glm::vec4& color) {
    unsigned char pixels[4];
    pixels[0] = static_cast<unsigned char>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
    pixels[1] = static_cast<unsigned char>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
    pixels[2] = static_cast<unsigned char>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
    pixels[3] = static_cast<unsigned char>(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f);

    CreateFromPixels(pixels, 1, 1);
}

void Texture::GenerateCheckerboard(int width, int height, const glm::vec4& color1, const glm::vec4& color2, int cellSize) {
    std::vector<unsigned char> pixels(width * height * 4);

    unsigned char c1[4] = {
        static_cast<unsigned char>(glm::clamp(color1.r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color1.g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color1.b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color1.a, 0.0f, 1.0f) * 255.0f)
    };

    unsigned char c2[4] = {
        static_cast<unsigned char>(glm::clamp(color2.r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color2.g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color2.b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(color2.a, 0.0f, 1.0f) * 255.0f)
    };

    if (cellSize <= 0) cellSize = 1;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool check = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            int idx = (y * width + x) * 4;
            unsigned char* src = check ? c1 : c2;

            pixels[idx + 0] = src[0];
            pixels[idx + 1] = src[1];
            pixels[idx + 2] = src[2];
            pixels[idx + 3] = src[3];
        }
    }

    CreateFromPixels(pixels.data(), width, height);
}

void Texture::GenerateGradient(int width, int height, const glm::vec4& startColor, const glm::vec4& endColor, bool isVertical) {
    std::vector<unsigned char> pixels(width * height * 4);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float t = 0.0f;
            if (isVertical) {
                t = static_cast<float>(y) / static_cast<float>(std::max(height - 1, 1));
            }
            else {
                t = static_cast<float>(x) / static_cast<float>(std::max(width - 1, 1));
            }

            glm::vec4 c = glm::mix(startColor, endColor, t);
            int idx = (y * width + x) * 4;

            pixels[idx + 0] = static_cast<unsigned char>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<unsigned char>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<unsigned char>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = static_cast<unsigned char>(glm::clamp(c.a, 0.0f, 1.0f) * 255.0f);
        }
    }

    CreateFromPixels(pixels.data(), width, height);
}

void Texture::CreateFromPixels(const unsigned char* pixels, int width, int height) {
    // Safety check: if texture already exists, clean it up
    if (image != VK_NULL_HANDLE) {
        Cleanup();
    }

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    // 1. Create Staging Buffer
    VulkanBuffer stagingBuffer(device, physicalDevice);
    stagingBuffer.CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Copy data to staging buffer
    stagingBuffer.CopyData(pixels, imageSize);

    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    // 2. Create Image (Using VulkanUtils)
    VulkanUtils::CreateImage(
        device, physicalDevice,
        static_cast<uint32_t>(width), static_cast<uint32_t>(height),
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
        static_cast<uint32_t>(width), static_cast<uint32_t>(height)
    );

    VulkanUtils::TransitionImageLayout(
        device, commandPool, graphicsQueue,
        image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    // 4. Create Image View (Using VulkanUtils)
    imageView = VulkanUtils::CreateImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT);

    // 5. Create Sampler 
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
}

void Texture::Cleanup() const {
    VulkanUtils::CleanupImageResources(device, image, imageMemory, imageView, sampler);
}