#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "Renderer.h"
#include "../vulkan/Vertex.h"
#include "../vulkan/VulkanUtils.h"
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <iostream>
#include <array>

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

Renderer::Renderer(VulkanDevice* deviceArg, VulkanSwapChain* swapChainArg)
    : device(deviceArg), swapChain(swapChainArg) {
}

void Renderer::Initialize() {
    CreateRenderPass();

    // 1. Main Offscreen Framebuffer (Color + Depth)
    CreateOffScreenResources();
    renderPass->CreateOffScreenFramebuffer(offScreenImageView, depthImageView, swapChain->GetExtent());

    // 2. Refraction Framebuffer
    {
        const std::array<VkImageView, 2> attachments = {
            refractionImageView,
            depthImageView // Reuse depth buffer
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass->GetRenderPass();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChain->GetExtent().width;
        framebufferInfo.height = swapChain->GetExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &refractionFramebuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create refraction framebuffer!");
        }
    }

    CreateUniformBuffers();
    CreateCommandBuffer();

    CreateTextureDescriptorSetLayout();
    CreateTextureDescriptorPool();
    CreateDefaultTexture();

    // Global Descriptor Set (UBOs)
    descriptorSet = std::make_unique<VulkanDescriptorSet>(device->GetDevice());
    descriptorSet->CreateDescriptorSetLayout();

    // Initialize Skybox
    skyboxPass = std::make_unique<SkyboxPass>(
        device->GetDevice(), device->GetPhysicalDevice(),
        commandBuffer->GetCommandPool(), device->GetGraphicsQueue()
    );
    skyboxPass->Initialize(renderPass->GetRenderPass(), swapChain->GetExtent(), descriptorSet->GetLayout());

    CreateShadowPass();

    // Create Descriptor Sets
    descriptorSet->CreateDescriptorPool(MAX_FRAMES_IN_FLIGHT);

    std::vector<VkBuffer> buffers;
    for (const auto& uniformBuffer : uniformBuffers) buffers.push_back(uniformBuffer->GetBuffer());

    descriptorSet->CreateDescriptorSets(
        buffers,
        sizeof(UniformBufferObject),
        shadowPass->GetShadowImageView(),
        shadowPass->GetShadowSampler(),
        refractionImageView,
        refractionSampler
    );

    // --- Create Shared Particle Pipelines ---
    CreateParticlePipelines();

    CreatePipeline(); // Main scene object pipeline
    CreateSyncObjects();

    CreateImGuiResources();
}

void Renderer::CreateImGuiResources() {
    // 1. Create Descriptor Pool for ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(device->GetDevice(), &pool_info, nullptr, &imguiPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create imgui descriptor pool!");
    }

    // 2. Create Render Pass for UI (LoadOp = LOAD to draw on top of scene)
    VkAttachmentDescription attachment = {};
    attachment.format = swapChain->GetImageFormat();
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // IMPORTANT: Keep existing content
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Coming from Copy Pass
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Going to Presentation

    VkAttachmentReference color_attachment = {};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (vkCreateRenderPass(device->GetDevice(), &info, nullptr, &uiRenderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ui render pass!");
    }

    // 3. Create Framebuffers
    const auto& imageViews = swapChain->GetImageViews();
    uiFramebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); i++) {
        VkImageView attachments[] = { imageViews[i] };
        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = uiRenderPass;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = attachments;
        fb_info.width = swapChain->GetExtent().width;
        fb_info.height = swapChain->GetExtent().height;
        fb_info.layers = 1;

        if (vkCreateFramebuffer(device->GetDevice(), &fb_info, nullptr, &uiFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create ui framebuffer!");
        }
    }

    // 4. Init ImGui Vulkan Implementation
    ImGui_ImplVulkan_InitInfo init_info = {};
    //init_info.Instance = device->GetInstance(); // Need to expose this or pass it
    init_info.PhysicalDevice = device->GetPhysicalDevice();
    init_info.Instance = device->GetInstance();
    init_info.Device = device->GetDevice();
    init_info.QueueFamily = device->GetQueueFamilies().graphicsFamily.value();
    init_info.Queue = device->GetGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiPool;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2; // Assume 2 for now, or expose from swapchain
    init_info.ImageCount = static_cast<uint32_t>(imageViews.size());
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr; // Or add a check function

    ImGui_ImplVulkan_Init(&init_info, uiRenderPass);

    // 5. Upload Fonts
    VkCommandBuffer cmd = commandBuffer->GetCommandBuffer(0); // Temporary use 0

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    ImGui_ImplVulkan_CreateFontsTexture(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    vkQueueSubmit(device->GetGraphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);
    vkDeviceWaitIdle(device->GetDevice());

    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void Renderer::DrawUI(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkRenderPassBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = uiRenderPass;
    info.framebuffer = uiFramebuffers[imageIndex];
    info.renderArea.extent = swapChain->GetExtent();
    info.clearValueCount = 0; // No clear, we load

    vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);
}

void Renderer::DrawFrame(const Scene& scene, uint32_t currentFrame, const glm::mat4& viewMatrix, const glm::mat4& projMatrix, int layerMask) {
    // Wait for this frame's fence
    const VkFence fence = syncObjects->GetInFlightFence(currentFrame);
    vkWaitForFences(device->GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX);

    // Acquire next image
    uint32_t imageIndex;
    const VkResult result = vkAcquireNextImageKHR(
        device->GetDevice(),
        swapChain->GetSwapChain(),
        UINT64_MAX,
        syncObjects->GetImageAvailableSemaphore(currentFrame),
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    // Check if this image is already in use
    VkFence& imageInFlightFence = syncObjects->GetImageInFlight(imageIndex);
    if (imageInFlightFence != VK_NULL_HANDLE) {
        vkWaitForFences(device->GetDevice(), 1, &imageInFlightFence, VK_TRUE, UINT64_MAX);
    }
    imageInFlightFence = fence;

    vkResetFences(device->GetDevice(), 1, &fence);

    VkCommandBuffer cmd = commandBuffer->GetCommandBuffer(currentFrame);
    RecordCommandBuffer(cmd, imageIndex, currentFrame, scene, viewMatrix, projMatrix, layerMask);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    const VkSemaphore waitSemaphore = syncObjects->GetImageAvailableSemaphore(currentFrame);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    const VkSemaphore signalSemaphore = syncObjects->GetRenderFinishedSemaphore(imageIndex);
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    if (vkQueueSubmit(device->GetGraphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;

    const VkSwapchainKHR swapChainHandle = swapChain->GetSwapChain();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChainHandle;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(device->GetPresentQueue(), &presentInfo);
}

void Renderer::CreateShadowPass() {
    shadowPass = std::make_unique<ShadowPass>(device, 16384, 16384);
    shadowPass->Initialize(descriptorSet->GetLayout());
}

void Renderer::CreateUniformBuffers() {
    const VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers[i] = std::make_unique<VulkanBuffer>(device->GetDevice(), device->GetPhysicalDevice());
        uniformBuffers[i]->CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkMapMemory(device->GetDevice(), uniformBuffers[i]->GetBufferMemory(), 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
}

void Renderer::CreateTextureDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0; // Binding 0 in Set 1
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    if (vkCreateDescriptorSetLayout(device->GetDevice(), &layoutInfo, nullptr, &textureSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture set layout!");
    }
}

void Renderer::CreateTextureDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 100;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 100;

    if (vkCreateDescriptorPool(device->GetDevice(), &poolInfo, nullptr, &textureDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture descriptor pool!");
    }
}

void Renderer::RegisterProceduralTexture(const std::string& name, const std::function<void(Texture&)>& generator) {
    // 1. Create the Texture Object
    auto tex = std::make_unique<Texture>(
        device->GetDevice(), device->GetPhysicalDevice(),
        commandBuffer->GetCommandPool(), device->GetGraphicsQueue());

    // 2. Run the user's generation function (e.g., GenerateCheckerboard)
    generator(*tex);

    // 3. Allocate Descriptor Set (Logic similar to CreateDefaultTexture)
    VkDescriptorSet descSet;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = textureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureSetLayout;

    if (vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor set for procedural texture!");
    }

    // 4. Update Descriptor Set
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = tex->GetImageView();
    imageInfo.sampler = tex->GetSampler();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device->GetDevice(), 1, &descriptorWrite, 0, nullptr);

    // 5. Store in Cache
    textureCache[name] = { std::move(tex), descSet };
    std::cout << "Procedural Texture Registered: " << name << std::endl;
}

void Renderer::CreateDefaultTexture() {
    defaultTextureResource.texture = std::make_unique<Texture>(
        device->GetDevice(), device->GetPhysicalDevice(),
        commandBuffer->GetCommandPool(), device->GetGraphicsQueue());

    defaultTextureResource.texture->LoadFromFile("textures/default.png");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = textureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureSetLayout;

    vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &defaultTextureResource.descriptorSet);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = defaultTextureResource.texture->GetImageView();
    imageInfo.sampler = defaultTextureResource.texture->GetSampler();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = defaultTextureResource.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device->GetDevice(), 1, &descriptorWrite, 0, nullptr);
}

VkDescriptorSet Renderer::GetTextureDescriptorSet(const std::string& path) {
    if (path.empty()) return defaultTextureResource.descriptorSet;

    const auto it = textureCache.find(path);
    if (it != textureCache.end()) {
        return it->second.descriptorSet;
    }

    auto tex = std::make_unique<Texture>(
        device->GetDevice(), device->GetPhysicalDevice(),
        commandBuffer->GetCommandPool(), device->GetGraphicsQueue());

    if (!tex->LoadFromFile(path)) {
        std::cerr << "Failed to load texture: " << path << ", using default." << std::endl;
        return defaultTextureResource.descriptorSet;
    }

    VkDescriptorSet descSet;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = textureDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureSetLayout;

    vkAllocateDescriptorSets(device->GetDevice(), &allocInfo, &descSet);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = tex->GetImageView();
    imageInfo.sampler = tex->GetSampler();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device->GetDevice(), 1, &descriptorWrite, 0, nullptr);

    textureCache[path] = { std::move(tex), descSet };
    return descSet;
}

void Renderer::UpdateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo) {
    memcpy(uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
}

static bool HasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates,
    VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (const VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

VkFormat findDepthFormat(VkPhysicalDevice physicalDevice) {
    return findSupportedFormat(physicalDevice,
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void Renderer::CreateRenderPass() {
    renderPass = std::make_unique<VulkanRenderPass>(
        device->GetDevice(),
        swapChain->GetImageFormat()
    );
    renderPass->Create(true);
}

void Renderer::CreatePipeline() {
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    GraphicsPipelineConfig pipelineConfig{};
    pipelineConfig.vertShaderPath = "src/shaders/vert.spv";
    pipelineConfig.fragShaderPath = "src/shaders/frag.spv";
    pipelineConfig.renderPass = renderPass->GetRenderPass();
    pipelineConfig.extent = swapChain->GetExtent();
    pipelineConfig.bindingDescription = &bindingDescription;
    pipelineConfig.attributeDescriptions = attributeDescriptions.data();
    pipelineConfig.attributeCount = static_cast<uint32_t>(attributeDescriptions.size());
    pipelineConfig.descriptorSetLayouts = {
        descriptorSet->GetLayout(),
        textureSetLayout
    };

    pipelineConfig.cullMode = VK_CULL_MODE_BACK_BIT;
    pipelineConfig.depthTestEnable = true;
    pipelineConfig.depthWriteEnable = true;
    pipelineConfig.blendEnable = true;

    graphicsPipeline = std::make_unique<GraphicsPipeline>(device->GetDevice(), pipelineConfig);
    graphicsPipeline->Create();
}

void Renderer::CreateOffScreenResources() {
    const VkExtent2D extent = swapChain->GetExtent();
    const VkFormat imageFormat = swapChain->GetImageFormat();

    // 1. Main OffScreen Color Attachment
    VulkanUtils::CreateImage(
        device->GetDevice(),
        device->GetPhysicalDevice(),
        extent.width, extent.height, 1, 1,
        imageFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        offScreenImage,
        offScreenImageMemory
    );

    offScreenImageView = VulkanUtils::CreateImageView(
        device->GetDevice(), offScreenImage, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT
    );

    // 2. Refraction Color Attachment
    VulkanUtils::CreateImage(
        device->GetDevice(),
        device->GetPhysicalDevice(),
        extent.width, extent.height, 1, 1,
        imageFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        refractionImage,
        refractionImageMemory
    );

    refractionImageView = VulkanUtils::CreateImageView(
        device->GetDevice(), refractionImage, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT
    );

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device->GetDevice(), &samplerInfo, nullptr, &refractionSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create refraction sampler!");
    }

    // 3. Shared Depth Attachment
    const VkFormat depthFormat = findDepthFormat(device->GetPhysicalDevice());

    VulkanUtils::CreateImage(
        device->GetDevice(),
        device->GetPhysicalDevice(),
        extent.width, extent.height, 1, 1,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        depthImage,
        depthImageMemory
    );

    depthImageView = VulkanUtils::CreateImageView(
        device->GetDevice(), depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT
    );
}

void Renderer::BeginRenderPass(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer fb, const std::vector<VkClearValue>& clearValues) const {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = pass;
    renderPassInfo.framebuffer = fb;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapChain->GetExtent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(swapChain->GetExtent().width);
    viewport.height = static_cast<float>(swapChain->GetExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = swapChain->GetExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void Renderer::RenderRefractionPass(VkCommandBuffer cmd, uint32_t currentFrame, const Scene& scene, int layerMask) {
    std::vector<VkClearValue> clearValues(2);
    clearValues[0].color = { {0.1f, 0.1f, 0.1f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    BeginRenderPass(cmd, renderPass->GetRenderPass(), refractionFramebuffer, clearValues);

    if (skyboxPass) {
        skyboxPass->Draw(cmd, scene, currentFrame, descriptorSet->GetDescriptorSets()[currentFrame]);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->GetPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->GetLayout(), 0, 1, &descriptorSet->GetDescriptorSets()[currentFrame], 0, nullptr);

    for (const auto& obj : scene.GetObjects()) {
        if (!obj || !obj->visible || !obj->geometry || obj->shadingMode == 3 || obj->shadingMode == 2 || obj->shadingMode == 4) continue;
        if ((obj->layerMask & layerMask) == 0) continue;

        PushConstantObject pco{};
        pco.model = obj->transform;
        pco.shadingMode = obj->shadingMode;
        pco.receiveShadows = obj->receiveShadows ? 1 : 0;
        pco.layerMask = obj->layerMask;
        vkCmdPushConstants(cmd, graphicsPipeline->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantObject), &pco);

        const VkDescriptorSet textureSet = GetTextureDescriptorSet(obj->texturePath);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->GetLayout(), 1, 1, &textureSet, 0, nullptr);

        obj->geometry->Bind(cmd);
        obj->geometry->Draw(cmd);
    }
    vkCmdEndRenderPass(cmd);

    // Barrier for refraction texture read
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = refractionImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Renderer::CreateCommandBuffer() {
    commandBuffer = std::make_unique<VulkanCommandBuffer>(
        device->GetDevice(),
        device->GetPhysicalDevice()
    );
    commandBuffer->CreateCommandPool(device->GetQueueFamilies().graphicsFamily.value());
    commandBuffer->CreateCommandBuffers(MAX_FRAMES_IN_FLIGHT);
}

void Renderer::CreateSyncObjects() {
    syncObjects = std::make_unique<VulkanSyncObjects>(
        device->GetDevice(),
        MAX_FRAMES_IN_FLIGHT
    );

    const uint32_t imageCount = static_cast<uint32_t>(swapChain->GetImages().size());
    if (imageCount == 0) {
        throw std::runtime_error("swap chain contains no images");
    }

    syncObjects->CreateSyncObjects(imageCount);
}

void Renderer::DrawSceneObjects(VkCommandBuffer cmd, const Scene& scene, VkPipelineLayout layout, bool bindTextures, bool skipIfNotCastingShadow, int layerMask) {
    for (const auto& obj : scene.GetObjects()) {
        if (!obj || !obj->visible || !obj->geometry) continue;
        if ((obj->layerMask & layerMask) == 0) continue;
        if (skipIfNotCastingShadow && !obj->castsShadow) continue;

        PushConstantObject pco{};
        pco.model = obj->transform;
        pco.shadingMode = obj->shadingMode;
        pco.receiveShadows = obj->receiveShadows ? 1 : 0;
        pco.layerMask = obj->layerMask;
        pco.burnFactor = obj->burnFactor;
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantObject), &pco);

        if (bindTextures) {
            const VkDescriptorSet textureSet = GetTextureDescriptorSet(obj->texturePath);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &textureSet, 0, nullptr);
        }

        obj->geometry->Bind(cmd);
        obj->geometry->Draw(cmd);
    }
}

void Renderer::CreateParticlePipelines() {
    auto bindings = ParticleSystem::GetBindingDescriptions();
    auto attribs = ParticleSystem::GetAttributeDescriptions();

    GraphicsPipelineConfig config{};
    config.vertShaderPath = "src/shaders/particle_vert.spv";
    config.fragShaderPath = "src/shaders/particle_frag.spv";
    config.renderPass = renderPass->GetRenderPass();
    config.extent = swapChain->GetExtent();

    config.bindingDescription = bindings.data();
    config.bindingCount = static_cast<uint32_t>(bindings.size());
    config.attributeDescriptions = attribs.data();
    config.attributeCount = static_cast<uint32_t>(attribs.size());

    config.descriptorSetLayouts = { descriptorSet->GetLayout(), textureSetLayout };

    config.depthWriteEnable = false;
    config.depthTestEnable = true;
    config.blendEnable = true;

    // Additive Pipeline
    config.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    config.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;

    particlePipelineAdditive = std::make_unique<GraphicsPipeline>(device->GetDevice(), config);
    particlePipelineAdditive->Create();

    // Alpha Blended Pipeline
    config.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    config.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    particlePipelineAlpha = std::make_unique<GraphicsPipeline>(device->GetDevice(), config);
    particlePipelineAlpha->Create();
}

void Renderer::SetupSceneParticles(Scene& scene) const {
    scene.SetupParticleSystem(
        commandBuffer->GetCommandPool(),
        device->GetGraphicsQueue(),
        particlePipelineAdditive.get(),
        particlePipelineAlpha.get(),
        textureSetLayout,
        MAX_FRAMES_IN_FLIGHT
    );
}

void Renderer::RenderShadowMap(VkCommandBuffer cmd, uint32_t currentFrame, const Scene& scene, int layerMask) {
    shadowPass->Begin(cmd);

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        shadowPass->GetPipeline()->GetLayout(),
        0,
        1,
        &descriptorSet->GetDescriptorSets()[currentFrame],
        0,
        nullptr
    );

    DrawSceneObjects(
        cmd,
        scene,
        shadowPass->GetPipeline()->GetLayout(),
        false, // bindTextures
        true,  // skipIfNotCastingShadow
        layerMask
    );

    shadowPass->End(cmd);
}

void Renderer::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex,
    uint32_t currentFrame, const Scene& scene,
    const glm::mat4& viewMatrix, const glm::mat4& projMatrix, int layerMask) {

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    // --- 0. Update UBO ---
    glm::vec3 lightPos = glm::vec3(0.0f, 200.0f, 0.0f);
    const auto& lights = scene.GetLights();
    if (!lights.empty()) lightPos = lights[0].position;

    glm::mat4 lightProj = glm::ortho(-200.0f, 200.0f, -200.0f, 200.0f, 1.0f, 500.0f);
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    lightProj[1][1] *= -1;
    const glm::mat4 lightSpaceMatrix = lightProj * lightView;

    UniformBufferObject ubo{};
    ubo.view = viewMatrix;
    ubo.proj = projMatrix;
    ubo.viewPos = glm::vec3(glm::inverse(viewMatrix)[3]);
    ubo.lightSpaceMatrix = lightSpaceMatrix;
    const size_t count = std::min(lights.size(), static_cast<size_t>(MAX_LIGHTS));
    if (count > 0) std::memcpy(ubo.lights, lights.data(), count * sizeof(Light));
    ubo.numLights = static_cast<int>(count);

    float factor = 1.0f;
    if (!lights.empty()) {
        const float sunY = lights[0].position.y;
        const float lower = -50.0f;
        const float upper = 50.0f;
        const float t = (sunY - lower) / (upper - lower);
        factor = std::max(0.0f, std::min(t, 1.0f));
    }
    if (scene.IsPrecipitating()) {
        factor *= 0.75f;
    }

    ubo.dayNightFactor = factor;

    UpdateUniformBuffer(currentFrame, ubo);

    // --- 1. Render Shadow Pass ---
    RenderShadowMap(cmd, currentFrame, scene, SceneLayers::ALL);

    // --- 2. Render Refraction Pass ---
    RenderRefractionPass(cmd, currentFrame, scene, SceneLayers::INSIDE | SceneLayers::OUTSIDE);

    // --- 3. Render Main Scene ---
    RenderScene(cmd, currentFrame, scene, layerMask);

    // --- 4. Copy to SwapChain ---
    CopyOffScreenToSwapChain(cmd, imageIndex);

    DrawUI(cmd, imageIndex);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void Renderer::RenderScene(VkCommandBuffer cmd, uint32_t currentFrame, const Scene& scene, int layerMask) {
    std::vector<VkClearValue> clearValues(2);
    clearValues[0].color = { {m_ClearColor.r, m_ClearColor.g, m_ClearColor.b, m_ClearColor.a} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    BeginRenderPass(cmd, renderPass->GetRenderPass(), renderPass->GetOffScreenFramebuffer(), clearValues);

    if (skyboxPass) {
        skyboxPass->Draw(cmd, scene, currentFrame, descriptorSet->GetDescriptorSets()[currentFrame]);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->GetPipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->GetLayout(), 0, 1, &descriptorSet->GetDescriptorSets()[currentFrame], 0, nullptr);

    DrawSceneObjects(cmd, scene, graphicsPipeline->GetLayout(), true, false, layerMask);

    for (const auto& sys : scene.GetParticleSystems()) {
        sys->Draw(cmd, descriptorSet->GetDescriptorSets()[currentFrame], currentFrame);
    }

    vkCmdEndRenderPass(cmd);
}

void Renderer::CopyOffScreenToSwapChain(VkCommandBuffer cmd, uint32_t imageIndex) const {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = offScreenImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkImageMemoryBarrier swapChainBarrier{};
    swapChainBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapChainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapChainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    swapChainBarrier.image = swapChain->GetImages()[imageIndex];
    swapChainBarrier.subresourceRange = barrier.subresourceRange;
    swapChainBarrier.srcAccessMask = 0;
    swapChainBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &swapChainBarrier);

    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.extent = { swapChain->GetExtent().width, swapChain->GetExtent().height, 1 };

    vkCmdCopyImage(cmd,
        offScreenImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapChain->GetImages()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copyRegion);

    swapChainBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapChainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // Changed from PRESENT_SRC_KHR
    swapChainBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapChainBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // Changed

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // Changed
        0, 0, nullptr, 0, nullptr, 1, &swapChainBarrier);
}


void Renderer::WaitIdle() const {
    vkDeviceWaitIdle(device->GetDevice());
}

void Renderer::Cleanup() {
    if (uiRenderPass) { vkDestroyRenderPass(device->GetDevice(), uiRenderPass, nullptr); uiRenderPass = VK_NULL_HANDLE; }
    for (auto fb : uiFramebuffers) vkDestroyFramebuffer(device->GetDevice(), fb, nullptr);
    uiFramebuffers.clear();
    if (imguiPool) { vkDestroyDescriptorPool(device->GetDevice(), imguiPool, nullptr); imguiPool = VK_NULL_HANDLE; }

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffersMapped[i]) {
            vkUnmapMemory(device->GetDevice(), uniformBuffers[i]->GetBufferMemory());
        }
    }

    for (const auto& uniformBuffer : uniformBuffers) {
        if (uniformBuffer) {
            uniformBuffer->Cleanup();
        }
    }
    uniformBuffers.clear();
    uniformBuffersMapped.clear();

    if (descriptorSet) {
        descriptorSet->Cleanup();
        descriptorSet.reset();
    }

    for (auto& entry : textureCache) {
        if (entry.second.texture) {
            entry.second.texture->Cleanup();
            entry.second.texture.reset();
        }
        entry.second.descriptorSet = VK_NULL_HANDLE;
    }
    textureCache.clear();

    if (defaultTextureResource.texture) {
        defaultTextureResource.texture->Cleanup();
        defaultTextureResource.texture.reset();
    }
    defaultTextureResource.descriptorSet = VK_NULL_HANDLE;

    if (textureDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device->GetDevice(), textureDescriptorPool, nullptr);
        textureDescriptorPool = VK_NULL_HANDLE;
    }

    if (textureSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device->GetDevice(), textureSetLayout, nullptr);
        textureSetLayout = VK_NULL_HANDLE;
    }

    if (particlePipelineAdditive) {
        particlePipelineAdditive->Cleanup();
        particlePipelineAdditive.reset();
    }
    if (particlePipelineAlpha) {
        particlePipelineAlpha->Cleanup();
        particlePipelineAlpha.reset();
    }

    if (syncObjects) {
        syncObjects->Cleanup();
        syncObjects.reset();
    }

    if (commandBuffer) {
        commandBuffer->Cleanup();
        commandBuffer.reset();
    }

    if (graphicsPipeline) {
        graphicsPipeline->Cleanup();
        graphicsPipeline.reset();
    }

    if (shadowPass) {
        shadowPass->Cleanup();
        shadowPass.reset();
    }

    if (renderPass) {
        renderPass->Cleanup();
        renderPass.reset();
    }

    if (skyboxPass) {
        skyboxPass->Cleanup();
        skyboxPass.reset();
    }

    CleanupOffScreenResources();
}

void Renderer::CleanupOffScreenResources() {
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device->GetDevice(), depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(device->GetDevice(), depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    if (depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->GetDevice(), depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }

    if (offScreenImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device->GetDevice(), offScreenImageView, nullptr);
        offScreenImageView = VK_NULL_HANDLE;
    }
    if (offScreenImage != VK_NULL_HANDLE) {
        vkDestroyImage(device->GetDevice(), offScreenImage, nullptr);
        offScreenImage = VK_NULL_HANDLE;
    }
    if (offScreenImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->GetDevice(), offScreenImageMemory, nullptr);
        offScreenImageMemory = VK_NULL_HANDLE;
    }

    if (refractionFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device->GetDevice(), refractionFramebuffer, nullptr);
        refractionFramebuffer = VK_NULL_HANDLE;
    }
    if (refractionSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device->GetDevice(), refractionSampler, nullptr);
        refractionSampler = VK_NULL_HANDLE;
    }
    if (refractionImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device->GetDevice(), refractionImageView, nullptr);
        refractionImageView = VK_NULL_HANDLE;
    }
    if (refractionImage != VK_NULL_HANDLE) {
        vkDestroyImage(device->GetDevice(), refractionImage, nullptr);
        refractionImage = VK_NULL_HANDLE;
    }
    if (refractionImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->GetDevice(), refractionImageMemory, nullptr);
        refractionImageMemory = VK_NULL_HANDLE;
    }
}