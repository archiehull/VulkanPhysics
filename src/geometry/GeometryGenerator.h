#pragma once

#include "Geometry.h"
#include <glm/glm.hpp>
#include <memory>

class GeometryGenerator final {
public:
    // Static-only utility: prevent instantiation and inheritance
    GeometryGenerator() = delete;
    ~GeometryGenerator() = delete;

    GeometryGenerator(const GeometryGenerator&) = delete;
    GeometryGenerator& operator=(const GeometryGenerator&) = delete;
    GeometryGenerator(GeometryGenerator&&) = delete;
    GeometryGenerator& operator=(GeometryGenerator&&) = delete;

    static std::unique_ptr<Geometry> CreateCube(VkDevice device, VkPhysicalDevice physicalDevice);
    static std::unique_ptr<Geometry> CreateGrid(VkDevice device, VkPhysicalDevice physicalDevice,
        int rows, int cols, float cellSize = 0.1f);
    static std::unique_ptr<Geometry> CreateSphere(VkDevice device, VkPhysicalDevice physicalDevice,
        int stacks = 16, int slices = 32, float radius = 0.5f);

    static std::unique_ptr<Geometry> CreateTerrain(VkDevice device, VkPhysicalDevice physicalDevice,
        float radius, int rings, int segments, float heightScale, float noiseFreq);
    static float GetTerrainHeight(float x, float z, float radius, float heightScale, float noiseFreq);

    static std::unique_ptr<Geometry> CreateBowl(VkDevice device, VkPhysicalDevice physicalDevice,
        float radius, int slices, int stacks);

    static std::unique_ptr<Geometry> CreatePedestal(VkDevice device, VkPhysicalDevice physicalDevice,
        float topRadius, float baseWidth, float height, int slices, int stacks);

    static std::unique_ptr<Geometry> CreateDisk(VkDevice device, VkPhysicalDevice physicalDevice, float radius, int slices);

private:
    static glm::vec3 GenerateColor(int index, int total);
    static void GenerateGridIndices(Geometry* geometry, int slices, int stacks);
};