#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <array>
#include "GraphicsPipeline.h"
#include "Texture.h"
#include "../vulkan/VulkanBuffer.h"

struct ParticleProps {
    glm::vec3 position = glm::vec3(0.0f);
    // NEW: Allow spawning in an area (Box Emitter)
    glm::vec3 positionVariation = glm::vec3(0.0f);
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 velocityVariation = glm::vec3(0.0f);
    glm::vec4 colorBegin = glm::vec4(1.0f);
    glm::vec4 colorEnd = glm::vec4(1.0f);
    float sizeBegin = 1.0f, sizeEnd = 1.0f, sizeVariation = 0.0f;
    float lifeTime = 1.0f;
    bool isAdditive = false;
    std::string texturePath;
};

class ParticleSystem final {
public:
    ParticleSystem(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg, VkCommandPool commandPool, VkQueue graphicsQueue, uint32_t maxParticlesArg, uint32_t framesInFlightArg);
    ~ParticleSystem();

    // Non-copyable
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    // Movable
    ParticleSystem(ParticleSystem&&) noexcept = default;
    ParticleSystem& operator=(ParticleSystem&&) noexcept = default;

    // Change: Added isAdditive parameter
    void Initialize(VkDescriptorSetLayout textureLayoutArg, GraphicsPipeline* pipelineArg, const std::string& texturePathArg, bool isAdditiveArg);

    // Set constraints for particle movement
    void SetSimulationBounds(const glm::vec3& center, float radius);

    void Update(float dt);
    void Draw(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSet, uint32_t currentFrame);

    void Emit(const ParticleProps& props);
    int AddEmitter(const ParticleProps& props, float particlesPerSecond);
    void StopEmitter(int emitterId);
    void UpdateEmitter(int emitterId, const ParticleProps& props, float particlesPerSecond);

    std::string GetTexturePath() const { return texture ? texturePath : ""; }

    void SetPipeline(GraphicsPipeline* newPipeline) { pipeline = newPipeline; }
    bool IsAdditive() const { return isAdditive; }

    // Data sent to GPU per instance (Modified for 16-byte alignment)
    struct InstanceData {
        glm::vec4 position; // xyz = position, w = padding (Offset 0)
        glm::vec4 color;    // rgba (Offset 16)
        glm::vec4 size;     // x = size, yzw = padding   (Offset 32)
    };

    // Static helpers to describe vertex input for the shared pipeline
    static std::array<VkVertexInputBindingDescription, 2> GetBindingDescriptions();
    static std::array<VkVertexInputAttributeDescription, 5> GetAttributeDescriptions();

private:
    struct Particle {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec4 colorBegin = glm::vec4(1.0f);
        glm::vec4 colorEnd = glm::vec4(1.0f);
        float sizeBegin = 0.0f;
        float sizeEnd = 0.0f;
        float lifeTime = 0.0f;
        float lifeRemaining = 0.0f;
        bool active = false;
        float camDistance = -1.0f;
    };

    struct ParticleEmitter {
        int id;
        ParticleProps props;
        float particlesPerSecond = 0.0f;
        float timeSinceLastEmit = 0.0f;
    };

    int nextEmitterId = 0;

    // Device handles first for compact layout
    VkDevice device;
    VkPhysicalDevice physicalDevice;

    // Small PODs next
    uint32_t maxParticles;
    uint32_t framesInFlight;
    uint32_t poolIndex = 9999;

    // Simulation state
    bool useBounds = false;
    glm::vec3 boundsCenter = glm::vec3(0.0f);
    float boundsRadius = 0.0f;

    // Texture/meta
    std::string texturePath;
    bool isAdditive = false;

    // Dynamic collections and heap resources
    std::vector<Particle> particles;
    std::vector<ParticleEmitter> emitters;
    std::vector<std::unique_ptr<VulkanBuffer>> instanceBuffers;
    std::unique_ptr<Texture> texture;
    std::unique_ptr<VulkanBuffer> vertexBuffer;

    // Rendering resources
    GraphicsPipeline* pipeline = nullptr;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureLayout = VK_NULL_HANDLE;

    void SetupBuffers();
    void UpdateInstanceBuffer(uint32_t currentFrame);
};