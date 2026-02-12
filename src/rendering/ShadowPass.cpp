#include "ShadowPass.h"
#include "../vulkan/Vertex.h"
#include "../vulkan/VulkanUtils.h"
#include <stdexcept>
#include <array>

ShadowPass::ShadowPass(VulkanDevice* deviceArg, uint32_t widthArg, uint32_t heightArg)
    : device(deviceArg), extent{ widthArg, heightArg } {
}

void ShadowPass::Initialize(VkDescriptorSetLayout globalSetLayout) {
    CreateResources();
    CreateRenderPass();
    CreateFramebuffer();
    CreatePipeline(globalSetLayout);
}

void ShadowPass::Begin(VkCommandBuffer cmd) const {
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = extent;

    VkClearValue clearValue{};
    clearValue.depthStencil = { 1.0f, 0 };
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetPipeline());

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdSetDepthBias(cmd, 0.5f, 0.0f, 0.5f);
}

void ShadowPass::CreateResources() {
    const VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    VulkanUtils::CreateImage(
        device->GetDevice(),
        device->GetPhysicalDevice(),
        extent.width, extent.height, 1, 1,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        shadowImage,
        shadowImageMemory
    );

    shadowImageView = VulkanUtils::CreateImageView(
        device->GetDevice(),
        shadowImage,
        depthFormat,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(device->GetDevice(), &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow sampler!");
    }
}

void ShadowPass::CreateRenderPass() {
    VkAttachmentDescription attachment{};
    attachment.format = VK_FORMAT_D32_SFLOAT;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device->GetDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow render pass!");
    }
}

void ShadowPass::CreateFramebuffer() {
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &shadowImageView;
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device->GetDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow framebuffer!");
    }
}

void ShadowPass::CreatePipeline(VkDescriptorSetLayout globalSetLayout) {
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    GraphicsPipelineConfig config{};
    config.vertShaderPath = "src/shaders/shadow_vert.spv";
    config.fragShaderPath = "src/shaders/shadow_frag.spv";
    config.renderPass = renderPass;
    config.extent = extent;
    config.bindingDescription = &bindingDescription;
    config.attributeDescriptions = attributeDescriptions.data();

    config.attributeCount = 1;

    config.descriptorSetLayouts = { globalSetLayout };

    config.cullMode = VK_CULL_MODE_NONE;
    config.depthBiasEnable = false;

    config.depthTestEnable = true;
    config.depthWriteEnable = true;

    pipeline = std::make_unique<GraphicsPipeline>(device->GetDevice(), config);
    pipeline->Create();
}

void ShadowPass::Cleanup() {
    if (pipeline) {
        pipeline->Cleanup();
        pipeline.reset();
    }

    if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device->GetDevice(), framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device->GetDevice(), renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
    if (shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device->GetDevice(), shadowSampler, nullptr);
        shadowSampler = VK_NULL_HANDLE;
    }
    if (shadowImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device->GetDevice(), shadowImageView, nullptr);
        shadowImageView = VK_NULL_HANDLE;
    }
    if (shadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(device->GetDevice(), shadowImage, nullptr);
        shadowImage = VK_NULL_HANDLE;
    }
    if (shadowImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device->GetDevice(), shadowImageMemory, nullptr);
        shadowImageMemory = VK_NULL_HANDLE;
    }
}