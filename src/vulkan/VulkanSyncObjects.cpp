#include "VulkanSyncObjects.h"
#include <stdexcept>

VulkanSyncObjects::VulkanSyncObjects(VkDevice deviceArg, uint32_t maxFramesInFlightArg)
    : device(deviceArg), maxFramesInFlight(maxFramesInFlightArg) {
}

void VulkanSyncObjects::CreateSyncObjects(uint32_t swapChainImageCount) {
    // imageAvailableSemaphores: one per frame-in-flight
    imageAvailableSemaphores.resize(maxFramesInFlight);
    // renderFinishedSemaphores: one per swapchain image
    renderFinishedSemaphores.resize(swapChainImageCount);

    // Create fences for frames in flight
    inFlightFences.resize(maxFramesInFlight);

    // Track which fence is using which image (one entry per swapchain image)
    imagesInFlight.resize(swapChainImageCount, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // Create one image-available semaphore per frame in flight
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create imageAvailable semaphore!");
        }
    }

    // Create one render-finished semaphore per swapchain image
    for (size_t i = 0; i < swapChainImageCount; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create renderFinished semaphore!");
        }
    }

    // Create fences for frames in flight
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        if (vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create fence for frame in flight!");
        }
    }
}

void VulkanSyncObjects::Cleanup() {
    for (const auto semaphore : imageAvailableSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }
    imageAvailableSemaphores.clear();

    for (const auto semaphore : renderFinishedSemaphores) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, semaphore, nullptr);
        }
    }
    renderFinishedSemaphores.clear();

    for (const auto fence : inFlightFences) {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
    }
    inFlightFences.clear();

    imagesInFlight.clear();
}

VkSemaphore VulkanSyncObjects::GetImageAvailableSemaphore(uint32_t frameIndex) const {
    if (frameIndex >= imageAvailableSemaphores.size()) {
        throw std::out_of_range("GetImageAvailableSemaphore: index out of range");
    }
    return imageAvailableSemaphores[frameIndex];
}

VkSemaphore VulkanSyncObjects::GetRenderFinishedSemaphore(uint32_t imageIndex) const {
    if (imageIndex >= renderFinishedSemaphores.size()) {
        throw std::out_of_range("GetRenderFinishedSemaphore: index out of range");
    }
    return renderFinishedSemaphores[imageIndex];
}

VkFence VulkanSyncObjects::GetInFlightFence(uint32_t currentFrame) const {
    if (currentFrame >= inFlightFences.size()) {
        throw std::out_of_range("GetInFlightFence: index out of range");
    }
    return inFlightFences[currentFrame];
}

VkFence& VulkanSyncObjects::GetImageInFlight(uint32_t imageIndex) {
    if (imageIndex >= imagesInFlight.size()) {
        throw std::out_of_range("GetImageInFlight: index out of range");
    }
    return imagesInFlight[imageIndex];
}