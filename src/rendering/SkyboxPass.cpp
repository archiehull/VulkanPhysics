#include "SkyboxPass.h"
#include "../vulkan/Vertex.h"
#include "../vulkan/PushConstantObject.h"
#include <filesystem>
#include <iostream>
#include "core/ECS.h"
#include "core/Components.h"

SkyboxPass::SkyboxPass(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg, VkCommandPool commandPoolArg, VkQueue graphicsQueueArg)
    : device(deviceArg), physicalDevice(physicalDeviceArg), commandPool(commandPoolArg), graphicsQueue(graphicsQueueArg) {
}

SkyboxPass::~SkyboxPass() {
    try {
        Cleanup();
    }
    catch (...) {
        // Ensure destructor does not allow exceptions to propagate.
    }
}

// Returns the six face file paths for a skybox named `name`.
std::vector<std::string> SkyboxPass::GetSkyboxFaces(const std::string& name) const {
    namespace fs = std::filesystem;

    const std::string base = "textures/skybox/" + name + "/";

    const std::vector<std::string> faces = {
        base + "px.png", // +X
        base + "nx.png", // -X
        base + "py.png", // +Y
        base + "ny.png", // -Y
        base + "pz.png", // +Z
        base + "nz.png"  // -Z
    };

    const std::vector<std::string> defaultFaces = {
        "textures/skybox/cubemap_0(+X).jpg", "textures/skybox/cubemap_1(-X).jpg",
        "textures/skybox/cubemap_2(+Y).jpg", "textures/skybox/cubemap_3(-Y).jpg",
        "textures/skybox/cubemap_4(+Z).jpg", "textures/skybox/cubemap_5(-Z).jpg"
    };

    try {
        for (const auto& f : faces) {
            // fs::exists may throw on some platforms/permissions; catch and fall back below.
            if (!fs::exists(f)) {
                std::cerr << "SkyboxPass: missing skybox face '" << f << "'. Falling back to default skybox.\n";
                return defaultFaces;
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "SkyboxPass: error checking skybox files for '" << name << "': " << e.what() << ". Falling back to default skybox.\n";
        return defaultFaces;
    }

    return faces;
}

void SkyboxPass::Initialize(VkRenderPass renderPass, const VkExtent2D& extent, VkDescriptorSetLayout globalSetLayout) {
    // 1. Initialize Cubemap
    cubemap = std::make_unique<Cubemap>(device, physicalDevice, commandPool, graphicsQueue);

    const auto faces = GetSkyboxFaces("desert");

    cubemap->LoadFromFiles(faces);

    // 2. Create Pipeline
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    GraphicsPipelineConfig config{};
    // Ensure these point to your compiled SPIR-V shaders
    config.vertShaderPath = "src/shaders/skybox_vert.spv";
    config.fragShaderPath = "src/shaders/skybox_frag.spv";

    config.renderPass = renderPass;
    config.extent = extent;
    config.bindingDescription = &bindingDescription;
    config.attributeDescriptions = attributeDescriptions.data();

    // FIX: Only use the first attribute (Position)
    // The Vertex struct has 4 attributes (Pos, Color, UV, Normal), but skybox.vert only uses Location 0.
    // This prevents the "Vertex attribute at location X not consumed" validation error.
    config.attributeCount = 1;

    // Layout: Set 0 = Global UBO, Set 1 = Cubemap
    config.descriptorSetLayouts = { globalSetLayout, cubemap->GetDescriptorSetLayout() };

    // Settings for "Crystal Ball" (viewable from inside)
    config.cullMode = VK_CULL_MODE_FRONT_BIT;
    config.depthTestEnable = true;
    config.depthWriteEnable = false;

    pipeline = std::make_unique<GraphicsPipeline>(device, config);
    pipeline->Create();
}

void SkyboxPass::Draw(VkCommandBuffer cmd, Scene& scene, uint32_t currentFrame, VkDescriptorSet globalDescriptorSet) const {
    static_cast<void>(currentFrame);

    if (!pipeline || !cubemap) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetPipeline());

    // Bind Global UBO (Set 0)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);

    // Bind Cubemap (Set 1)
    const VkDescriptorSet skySet = cubemap->GetDescriptorSet();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetLayout(), 1, 1, &skySet, 0, nullptr);

    // Render objects marked with shadingMode = 2 (Skybox) OR 3 (Combined)
    Registry& registry = scene.GetRegistry();
    for (Entity e : scene.GetRenderableEntities()) {
        if (!registry.HasComponent<RenderComponent>(e) || !registry.HasComponent<TransformComponent>(e)) continue;

        auto& render = registry.GetComponent<RenderComponent>(e);
        auto& transform = registry.GetComponent<TransformComponent>(e);

        // UPDATE: Allow mode 3 to be drawn by this pass (for the inside view)
        if (!render.visible || !render.geometry || (render.shadingMode != 2 && render.shadingMode != 3)) continue;

        PushConstantObject pco{};
        pco.model = transform.matrix;
        // For the inside view, we force Mode 2 (Pure Skybox) look
        pco.shadingMode = 2;

        vkCmdPushConstants(cmd, pipeline->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstantObject), &pco);

        render.geometry->Bind(cmd);
        render.geometry->Draw(cmd);
    }
}

void SkyboxPass::Cleanup() {
    if (pipeline) {
        pipeline->Cleanup();
        pipeline.reset();
    }
    if (cubemap) {
        cubemap->Cleanup();
        cubemap.reset();
    }
}