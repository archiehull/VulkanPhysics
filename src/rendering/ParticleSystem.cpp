#include "ParticleSystem.h"
#include <random>
#include <algorithm> 
#include <iostream>
#include <array>
#include <cstring> // for memcpy
#include <stdexcept>

// Helper for random numbers
static float RandomFloat(float min, float max) {
    static std::random_device rd;
    static std::mt19937 mt(rd());
    std::uniform_real_distribution<float> dist(min, max);
    return dist(mt);
}

ParticleSystem::ParticleSystem(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg, VkCommandPool commandPool, VkQueue graphicsQueue, uint32_t maxParticlesArg, uint32_t framesInFlightArg)
    : device(deviceArg),
    physicalDevice(physicalDeviceArg),
    maxParticles(maxParticlesArg),
    framesInFlight(framesInFlightArg),
    poolIndex(maxParticlesArg > 0 ? maxParticlesArg - 1 : 0),
    particles(maxParticlesArg),
    texture(std::make_unique<Texture>(deviceArg, physicalDeviceArg, commandPool, graphicsQueue)) {
    // Nothing left to assign in body; all members initialized above.
}

ParticleSystem::~ParticleSystem() {
    // Only destroy the descriptor pool if it was created.
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    texture.reset();
    vertexBuffer.reset();
    // Clear vector
    instanceBuffers.clear();
}

void ParticleSystem::Initialize(VkDescriptorSetLayout textureLayoutArg, GraphicsPipeline* pipelineArg, const std::string& texturePathArg, bool isAdditiveArg) {
    texturePath = texturePathArg;
    textureLayout = textureLayoutArg;
    pipeline = pipelineArg;
    isAdditive = isAdditiveArg;

    texture->LoadFromFile(texturePath);
    SetupBuffers();

    // Pool & Set creation remains here because each system needs its own descriptor set (for its specific texture)
    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create particle descriptor pool!");
    }

    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &textureLayout;
    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        // Clean up pool if allocation failed to avoid leaving a dangling pool
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
        throw std::runtime_error("failed to allocate particle descriptor set!");
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture->GetImageView();
    imageInfo.sampler = texture->GetSampler();

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void ParticleSystem::SetSimulationBounds(const glm::vec3& center, float radius) {
    boundsCenter = center;
    boundsRadius = radius;
    useBounds = true;
}

void ParticleSystem::Emit(const ParticleProps& props) {
    Particle& p = particles[poolIndex];
    p.active = true;

    p.position.x = props.position.x + props.positionVariation.x * RandomFloat(-1.0f, 1.0f);
    p.position.y = props.position.y + props.positionVariation.y * RandomFloat(-1.0f, 1.0f);
    p.position.z = props.position.z + props.positionVariation.z * RandomFloat(-1.0f, 1.0f);

    p.velocity = props.velocity;
    p.velocity.x += props.velocityVariation.x * RandomFloat(-1.0f, 1.0f);
    p.velocity.y += props.velocityVariation.y * RandomFloat(-1.0f, 1.0f);
    p.velocity.z += props.velocityVariation.z * RandomFloat(-1.0f, 1.0f);

    p.colorBegin = props.colorBegin;
    p.colorEnd = props.colorEnd;
    p.lifeTime = props.lifeTime;
    p.lifeRemaining = props.lifeTime;
    p.sizeBegin = props.sizeBegin + props.sizeVariation * RandomFloat(-1.0f, 1.0f);
    p.sizeEnd = props.sizeEnd;

    if (poolIndex == 0) {
        poolIndex = maxParticles - 1;
    }
    else {
        poolIndex--;
    }
}

int ParticleSystem::AddEmitter(const ParticleProps& props, float particlesPerSecond) {
    ParticleEmitter emitter;
    emitter.id = nextEmitterId++; // Assign and increment
    emitter.props = props;
    emitter.particlesPerSecond = particlesPerSecond;
    emitter.timeSinceLastEmit = 0.0f;
    emitters.push_back(emitter);

    return emitter.id;
}

void ParticleSystem::StopEmitter(int emitterId) {
    // Remove emitter with matching ID using erase-remove idiom
    emitters.erase(
        std::remove_if(emitters.begin(), emitters.end(),
            [emitterId](const ParticleEmitter& e) { return e.id == emitterId; }),
        emitters.end()
    );
}

void ParticleSystem::UpdateEmitter(int emitterId, const ParticleProps& props, float particlesPerSecond) {
    const auto it = std::find_if(emitters.begin(), emitters.end(),
        [emitterId](const ParticleEmitter& e) { return e.id == emitterId; });

    if (it != emitters.end()) {
        it->props = props;
        it->particlesPerSecond = particlesPerSecond;
    }
}

void ParticleSystem::Update(float dt) {
    for (auto& emitter : emitters) {
        emitter.timeSinceLastEmit += dt;
        const float emitInterval = 1.0f / emitter.particlesPerSecond;
        const float maxTime = 0.1f;
        if (emitter.timeSinceLastEmit > maxTime) emitter.timeSinceLastEmit = maxTime;
        while (emitter.timeSinceLastEmit >= emitInterval) {
            Emit(emitter.props);
            emitter.timeSinceLastEmit -= emitInterval;
        }
    }
    for (auto& p : particles) {
        if (!p.active) continue;
        if (p.lifeRemaining <= 0.0f) {
            p.active = false;
            continue;
        }
        p.lifeRemaining -= dt;
        p.position += p.velocity * dt;

        // --- Clamping Logic ---
        if (useBounds) {
            const float dist = glm::distance(p.position, boundsCenter);
            // If outside the radius, pull back to surface
            if (dist > boundsRadius) {
                // Avoid division by zero
                if (dist > 0.0001f) {
                    const glm::vec3 dir = (p.position - boundsCenter) / dist;
                    p.position = boundsCenter + dir * boundsRadius;
                }
            }
        }
    }
}

void ParticleSystem::UpdateInstanceBuffer(uint32_t currentFrame) {
    std::vector<InstanceData> instanceData;
    instanceData.reserve(maxParticles);

    for (const auto& p : particles) {
        if (!p.active) continue;

        const float lifeT = 1.0f - (p.lifeRemaining / p.lifeTime);

        InstanceData data{}; // Zero initialize

        // Explicitly convert vec3 -> vec4 (w=1.0)
        data.position = glm::vec4(p.position, 1.0f);

        // Color is already vec4
        data.color = glm::mix(p.colorBegin, p.colorEnd, lifeT);

        // Explicitly convert float -> vec4 (put size in x)
        const float currentSize = glm::mix(p.sizeBegin, p.sizeEnd, lifeT);
        data.size = glm::vec4(currentSize, 0.0f, 0.0f, 0.0f);

        instanceData.push_back(data);
    }

    if (!instanceData.empty()) {
        const VkDeviceSize size = static_cast<VkDeviceSize>(instanceData.size() * sizeof(InstanceData));
        void* dataPtr = nullptr;
        vkMapMemory(device, instanceBuffers[currentFrame]->GetBufferMemory(), 0, size, 0, &dataPtr);
        std::memcpy(dataPtr, instanceData.data(), static_cast<size_t>(size));
        vkUnmapMemory(device, instanceBuffers[currentFrame]->GetBufferMemory());
    }
}

void ParticleSystem::Draw(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSet, uint32_t currentFrame) {
    // Update the GPU buffer for THIS frame right before drawing
    UpdateInstanceBuffer(currentFrame);

    uint32_t activeCount = 0;
    for (const auto& p : particles) if (p.active) activeCount++;

    if (activeCount == 0 || !pipeline) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetPipeline());

    const std::array<VkDescriptorSet, 2> sets = { globalDescriptorSet, descriptorSet };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetLayout(), 0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    const std::array<VkBuffer, 1> vertexBuffers = { vertexBuffer->GetBuffer() };
    const std::array<VkDeviceSize, 1> offsets = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, static_cast<uint32_t>(vertexBuffers.size()), vertexBuffers.data(), offsets.data());

    // Bind the Instance Buffer for the CURRENT frame
    const std::array<VkBuffer, 1> instanceBufferRaw = { instanceBuffers[currentFrame]->GetBuffer() };
    vkCmdBindVertexBuffers(cmd, 1, static_cast<uint32_t>(instanceBufferRaw.size()), instanceBufferRaw.data(), offsets.data());

    vkCmdDraw(cmd, 6, activeCount, 0, 0);
}

void ParticleSystem::SetupBuffers() {
    // x, y, z, u, v (6 vertices * 5 floats = 30 floats)
    const std::array<float, 30> vertices = {
        -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
         0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
         0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f, 0.0f, 1.0f
    };

    vertexBuffer = std::make_unique<VulkanBuffer>(device, physicalDevice);
    vertexBuffer->CreateBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vertexBuffer->CopyData(vertices.data(), sizeof(vertices));

    instanceBuffers.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        instanceBuffers[i] = std::make_unique<VulkanBuffer>(device, physicalDevice);
        instanceBuffers[i]->CreateBuffer(maxParticles * sizeof(InstanceData),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
}

// Static definitions for Pipeline Creation
std::array<VkVertexInputBindingDescription, 2> ParticleSystem::GetBindingDescriptions() {
    std::array<VkVertexInputBindingDescription, 2> bindings{};

    // Binding 0: Mesh Data
    bindings[0].binding = 0;
    bindings[0].stride = 5 * sizeof(float); // x,y,z,u,v
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Binding 1: Instance Data
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(InstanceData);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    return bindings;
}

std::array<VkVertexInputAttributeDescription, 5> ParticleSystem::GetAttributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 5> attribs{};

    // Binding 0: Mesh Data (Unchanged)
    // Location 0: Position (vec3)
    attribs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
    // Location 1: UV (vec2)
    attribs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, 3 * sizeof(float) };

    // Binding 1: Instance Data (Modified for vec4 alignment)

    // Location 2: Position (Host sends vec4, Shader reads vec3. Uses xyz.)
    attribs[2] = { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, position) };

    // Location 3: Color (Host sends vec4, Shader reads vec4)
    attribs[3] = { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, color) };

    // Location 4: Size (Host sends vec4, Shader reads float. Uses x.)
    attribs[4] = { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(InstanceData, size) };

    return attribs;
}