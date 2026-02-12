#pragma once

#include "../vulkan/Vertex.h"
#include "../vulkan/VulkanBuffer.h"
#include <vector>
#include <memory>
#include <cstddef>
#include <utility>

class Geometry final {
public:
    Geometry(VkDevice deviceArg, VkPhysicalDevice physicalDeviceArg);
    ~Geometry() = default;

    // Non-copyable: class owns Vulkan resources
    Geometry(const Geometry&) = delete;
    Geometry& operator=(const Geometry&) = delete;

    // Movable
    Geometry(Geometry&&) noexcept = default;
    Geometry& operator=(Geometry&&) noexcept = default;

    void CreateBuffers();
    void Bind(VkCommandBuffer commandBuffer) const;
    void Draw(VkCommandBuffer commandBuffer) const;
    void Cleanup();

    // Read-only accessors (safe)
    const std::vector<Vertex>& GetVertices() const { return vertices; }
    const std::vector<uint32_t>& GetIndices() const { return indices; }

    bool HasIndices() const { return !indices.empty(); }

    // Controlled mutation API (replaces returning non-const references)
    void AddVertex(const Vertex& v) { vertices.push_back(v); }
    void AddVertex(Vertex&& v) { vertices.push_back(std::move(v)); }
    void AddIndex(uint32_t idx) { indices.push_back(idx); }

    void ReserveVertices(size_t n) { vertices.reserve(n); }
    void ReserveIndices(size_t n) { indices.reserve(n); }

    void SetIndices(const std::vector<uint32_t>& newIndices) { indices = newIndices; }

    // Random access when needed (safe single-element access)
    size_t VertexCount() const { return vertices.size(); }
    size_t IndexCount() const { return indices.size(); }

    Vertex& GetVertex(size_t idx) { return vertices[idx]; }
    const Vertex& GetVertex(size_t idx) const { return vertices[idx]; }

    uint32_t GetIndex(size_t idx) const { return indices[idx]; }

private:
    // Keep container data first for predictable layout
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    VkDevice device;
    VkPhysicalDevice physicalDevice;

    // Vulkan buffer members (vertex first for locality)
    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;
};