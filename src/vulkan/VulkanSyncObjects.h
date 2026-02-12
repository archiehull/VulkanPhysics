#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VulkanSyncObjects final {
public:
    VulkanSyncObjects(VkDevice device, uint32_t maxFramesInFlight);
    ~VulkanSyncObjects() = default;

    // Non-copyable (explicitly deleted)
    VulkanSyncObjects(const VulkanSyncObjects&) = delete;
    VulkanSyncObjects& operator=(const VulkanSyncObjects&) = delete;

    // Create sync objects:
    // - imageAvailableSemaphores: one per frame-in-flight (index by currentFrame)
    // - renderFinishedSemaphores: one per swapchain image (index by imageIndex)
    // - inFlightFences: one per frame-in-flight (index by currentFrame)
    // - imagesInFlight: one fence slot per swapchain image (index by imageIndex)
    void CreateSyncObjects(uint32_t swapChainImageCount);
    void Cleanup();

    // Accessors
    VkSemaphore GetImageAvailableSemaphore(uint32_t frameIndex) const;
    VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const;

    // Per-frame-in-flight fences (indexed by currentFrame)
    VkFence GetInFlightFence(uint32_t currentFrame) const;

    // Track which fence is using which image (indexed by imageIndex)
    VkFence& GetImageInFlight(uint32_t imageIndex);

private:
    VkDevice device;
    uint32_t maxFramesInFlight;

    // One semaphore per frame in flight
    std::vector<VkSemaphore> imageAvailableSemaphores;
    // One semaphore per swapchain image for render-finished
    std::vector<VkSemaphore> renderFinishedSemaphores;

    // One fence per frame in flight
    std::vector<VkFence> inFlightFences;

    // Track which frame's fence is using which swapchain image
    std::vector<VkFence> imagesInFlight;
};