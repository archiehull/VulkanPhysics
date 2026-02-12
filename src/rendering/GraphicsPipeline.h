#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <memory>
#include "../vulkan/Vertex.h"
#include "../vulkan/VulkanShader.h"

struct GraphicsPipelineConfig {
    std::string vertShaderPath;
    std::string fragShaderPath;

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

    VkVertexInputBindingDescription* bindingDescription = nullptr;
    VkVertexInputAttributeDescription* attributeDescriptions = nullptr;

    VkRenderPass renderPass = VK_NULL_HANDLE;

    VkExtent2D extent{ 0, 0 };

    uint32_t bindingCount = 1;
    uint32_t attributeCount = 0;

    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    float lineWidth = 1.0f;

    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;

    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;

    bool depthTestEnable = false;
    bool depthWriteEnable = false;
    bool depthBiasEnable = false;
    bool blendEnable = false;

    GraphicsPipelineConfig() = default;
    ~GraphicsPipelineConfig() = default;
    GraphicsPipelineConfig(const GraphicsPipelineConfig&) = default;
    GraphicsPipelineConfig& operator=(const GraphicsPipelineConfig&) = default;
    GraphicsPipelineConfig(GraphicsPipelineConfig&&) = default;
    GraphicsPipelineConfig& operator=(GraphicsPipelineConfig&&) = default;
};

class GraphicsPipeline final {
public:
    GraphicsPipeline(VkDevice deviceArg, const GraphicsPipelineConfig& configArg);
    ~GraphicsPipeline();

    // Non-copyable
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    // Movable
    GraphicsPipeline(GraphicsPipeline&&) noexcept = default;
    GraphicsPipeline& operator=(GraphicsPipeline&&) noexcept = default;

    void Create();
    void Cleanup();

    VkPipeline GetPipeline() const { return pipeline; }
    VkPipelineLayout GetLayout() const { return pipelineLayout; }

private:
    GraphicsPipelineConfig config;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_LINE_WIDTH
    };

    std::unique_ptr<VulkanShader> shader;

    VkDevice device;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};