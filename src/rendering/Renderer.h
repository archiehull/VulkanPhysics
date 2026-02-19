#pragma once

#include "SkyboxPass.h"
#include "Cubemap.h"
#include "Scene.h"
#include "../vulkan/VulkanDevice.h"
#include "../vulkan/VulkanSwapChain.h"
#include "../vulkan/VulkanRenderPass.h"
#include "../vulkan/VulkanCommandBuffer.h"
#include "../vulkan/VulkanSyncObjects.h"
#include "../vulkan/VulkanDescriptorSet.h"
#include "../vulkan/UniformBufferObject.h"
#include "../vulkan/PushConstantObject.h"
#include "../rendering/GraphicsPipeline.h"
#include "../rendering/Texture.h"
#include "../rendering/ShadowPass.h"
#include "ParticleSystem.h"

#include <memory>
#include <map>
#include <functional>
#include "../vulkan/VulkanContext.h"
#include "Camera.h"

class Renderer final {
public:
    Renderer(VulkanDevice* deviceArg, VulkanSwapChain* swapChainArg);
    ~Renderer() = default;

    // Non-copyable
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Initialize();

    void RegisterProceduralTexture(const std::string& name, const std::function<void(Texture&)>& generator);

    void DrawFrame(const Scene& scene, uint32_t currentFrame, const glm::mat4& viewMatrix, const glm::mat4& projMatrix, int layerMask = SceneLayers::ALL);
    void UpdateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo);
    void WaitIdle() const;
    void Cleanup();

    void SetupSceneParticles(Scene& scene) const;

    VulkanRenderPass* GetRenderPass() const { return renderPass.get(); }
    GraphicsPipeline* GetPipeline() const { return graphicsPipeline.get(); }

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    VkRenderPass uiRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> uiFramebuffers;

    void CreateImGuiResources();
    void DrawUI(VkCommandBuffer cmd, uint32_t imageIndex);

    void SetClearColor(const glm::vec4& color) { m_ClearColor = color; }

private:
    glm::vec4 m_ClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };

    // --- 1. Pointers & Smart Pointers (8-byte aligned) ---
    VulkanDevice* device;
    VulkanSwapChain* swapChain;
    Camera* m_camera = nullptr;
    VulkanContext* m_vulkanContext = nullptr;

    std::unique_ptr<VulkanRenderPass> renderPass;
    std::unique_ptr<GraphicsPipeline> graphicsPipeline;
    std::unique_ptr<VulkanCommandBuffer> commandBuffer;
    std::unique_ptr<VulkanSyncObjects> syncObjects;
    std::unique_ptr<ShadowPass> shadowPass;
    std::unique_ptr<SkyboxPass> skyboxPass;
    std::unique_ptr<VulkanDescriptorSet> descriptorSet;
    std::unique_ptr<Texture> texture;

    // Shared Particle Resources
    std::unique_ptr<GraphicsPipeline> particlePipelineAdditive;
    std::unique_ptr<GraphicsPipeline> particlePipelineAlpha;

    // --- 2. Vulkan Handles (Ptr/64-bit) ---
    VkImage refractionImage = VK_NULL_HANDLE;
    VkDeviceMemory refractionImageMemory = VK_NULL_HANDLE;
    VkImageView refractionImageView = VK_NULL_HANDLE;
    VkSampler refractionSampler = VK_NULL_HANDLE;
    VkFramebuffer refractionFramebuffer = VK_NULL_HANDLE;

    VkImage offScreenImage = VK_NULL_HANDLE;
    VkDeviceMemory offScreenImageMemory = VK_NULL_HANDLE;
    VkImageView offScreenImageView = VK_NULL_HANDLE;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool textureDescriptorPool = VK_NULL_HANDLE;

    // --- 3. Containers (Std Objects) ---
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;
    std::vector<void*> uniformBuffersMapped;
    std::vector<void*> m_uniformBuffersMapped;

    struct TextureResource {
        std::unique_ptr<Texture> texture;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

        TextureResource() = default;
        ~TextureResource() = default;
        TextureResource(const TextureResource&) = delete;
        TextureResource& operator=(const TextureResource&) = delete;
        TextureResource(TextureResource&&) = default;
        TextureResource& operator=(TextureResource&&) = default;
    };

    std::map<std::string, TextureResource> textureCache;
    TextureResource defaultTextureResource;

    // --- 4. Primitives ---
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    bool framebufferResized = false;

    // --- Methods ---
    void CreateParticlePipelines();
    void CreateTextureDescriptorSetLayout();
    void CreateTextureDescriptorPool();
    void CreateDefaultTexture();
    VkDescriptorSet GetTextureDescriptorSet(const std::string& path);

    void CreateRenderPass();
    void CreateShadowPass();
    void CreatePipeline();
    void CreateOffScreenResources();
    void CreateCommandBuffer();
    void CreateSyncObjects();
    void CreateUniformBuffers();

    void RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t currentFrame, const Scene& scene, const glm::mat4& viewMatrix, const glm::mat4& projMatrix, int layerMask);

    // Helper to reduce code duplication
    void BeginRenderPass(VkCommandBuffer cmd, VkRenderPass pass, VkFramebuffer fb, const std::vector<VkClearValue>& clearValues) const;

    void RenderShadowMap(VkCommandBuffer cmd, uint32_t currentFrame, const Scene& scene, int layerMask = SceneLayers::ALL);
    void DrawSceneObjects(VkCommandBuffer cmd, const Scene& scene, VkPipelineLayout layout, bool bindTextures, bool skipIfNotCastingShadow, int layerMask);
    void RenderScene(VkCommandBuffer cmd, uint32_t currentFrame, const Scene& scene, int layerMask);
    void RenderRefractionPass(VkCommandBuffer cmd, uint32_t currentFrame, const Scene& scene, int layerMask);

    void CopyOffScreenToSwapChain(VkCommandBuffer cmd, uint32_t imageIndex) const;
    void CleanupOffScreenResources();
};