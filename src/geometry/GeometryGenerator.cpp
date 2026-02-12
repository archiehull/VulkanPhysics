#include "GeometryGenerator.h"
#include <cmath>
#include <glm/gtc/noise.hpp>
#include <algorithm>

constexpr double PI = 3.14159265358979323846;

namespace {
    // Helper to set normal (unused in this specific refactor but kept if needed)
    void SetNormal(Vertex& v, const glm::vec3& n) { v.normal = n; }

    // [NEW] Helper to compute smooth normals by averaging face normals
    // This replaces the duplicated logic in CreatePedestal and CreateTerrain
    void ComputeSmoothNormals(Geometry* geometry) {
        // 1. Reset normals
        for (size_t i = 0; i < geometry->VertexCount(); ++i) {
            geometry->GetVertex(i).normal = glm::vec3(0.0f);
        }

        // 2. Accumulate face normals
        for (size_t i = 0; i < geometry->IndexCount(); i += 3) {
            const uint32_t i0 = geometry->GetIndex(i);
            const uint32_t i1 = geometry->GetIndex(i + 1);
            const uint32_t i2 = geometry->GetIndex(i + 2);

            const glm::vec3 v0 = geometry->GetVertex(i0).pos;
            const glm::vec3 v1 = geometry->GetVertex(i1).pos;
            const glm::vec3 v2 = geometry->GetVertex(i2).pos;

            const glm::vec3 edge1 = v1 - v0;
            const glm::vec3 edge2 = v2 - v0;
            const glm::vec3 normal = glm::cross(edge1, edge2);

            geometry->GetVertex(i0).normal += normal;
            geometry->GetVertex(i1).normal += normal;
            geometry->GetVertex(i2).normal += normal;
        }

        // 3. Normalize
        for (size_t i = 0; i < geometry->VertexCount(); ++i) {
            auto& n = geometry->GetVertex(i).normal;
            if (glm::length(n) > 0.00001f) {
                n = glm::normalize(n);
            }
            else {
                n = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
    }
}

float SmoothStep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void GeometryGenerator::GenerateGridIndices(Geometry* geometry, int slices, int stacks) {
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t first = i * (slices + 1) + j;
            const uint32_t second = first + (slices + 1);

            geometry->AddIndex(first);
            geometry->AddIndex(first + 1);
            geometry->AddIndex(second);

            geometry->AddIndex(first + 1);
            geometry->AddIndex(second + 1);
            geometry->AddIndex(second);
        }
    }
}

float GeometryGenerator::GetTerrainHeight(float x, float z, float radius, float heightScale, float noiseFreq) {
    const float dist = glm::length(glm::vec2(x, z));

    // 1. Noise Generation
    float y = 0.0f;
    // Base layer
    y += glm::perlin(glm::vec2(x, z) * noiseFreq);
    // Detail layer
    y += glm::perlin(glm::vec2(x, z) * noiseFreq * 2.0f) * 0.25f;

    y *= heightScale;

    // 2. Circular Clamping (Masking)
    const float edgeFactor = dist / radius;
    if (edgeFactor > 0.95f) {
        y *= 0.0f; // Force flat/down at edges
    }
    else {
        if (edgeFactor > 0.9f) {
            y *= 1.0f - ((edgeFactor - 0.9f) * 10.0f);
        }
    }

    return y;
}

std::unique_ptr<Geometry> GeometryGenerator::CreateDisk(VkDevice device, VkPhysicalDevice physicalDevice, float radius, int slices) {
    auto geometry = std::make_unique<Geometry>(device, physicalDevice);
    geometry->ReserveVertices(slices + 2);
    geometry->ReserveIndices(slices * 3);

    // Center
    geometry->AddVertex({ glm::vec3(0.0f), glm::vec3(0.0f), glm::vec2(0.5f), glm::vec3(0.0f, 1.0f, 0.0f) });

    // Rim
    for (int i = 0; i <= slices; ++i) {
        const float angle = (static_cast<float>(i) / slices) * 2.0f * PI;
        const float x = radius * cos(angle);
        const float z = radius * sin(angle);
        const float u = 0.5f + 0.5f * cos(angle);
        const float v = 0.5f + 0.5f * sin(angle);
        geometry->AddVertex({ glm::vec3(x, 0.0f, z), glm::vec3(1.0f), glm::vec2(u, v), glm::vec3(0.0f, 1.0f, 0.0f) });
    }

    for (int i = 1; i <= slices; ++i) {
        geometry->AddIndex(0);
        geometry->AddIndex(i + 1);
        geometry->AddIndex(i);
    }

    geometry->CreateBuffers();
    return geometry;
}

std::unique_ptr<Geometry> GeometryGenerator::CreateBowl(VkDevice device, VkPhysicalDevice physicalDevice, float radius, int slices, int stacks) {
    auto geometry = std::make_unique<Geometry>(device, physicalDevice);

    geometry->ReserveVertices((slices + 1) * (stacks + 1));
    geometry->ReserveIndices(slices * stacks * 6);

    for (int i = 0; i <= stacks; ++i) {
        const float v = static_cast<float>(i) / stacks;
        const float phi = static_cast<float>(PI) * 0.5f + (static_cast<float>(PI) * 0.5f * v);

        const float y = radius * cos(phi);
        const float r_at_y = radius * sin(phi);

        for (int j = 0; j <= slices; ++j) {
            const float u = static_cast<float>(j) / slices;
            const float theta = 2.0f * static_cast<float>(PI) * u;

            const float x = r_at_y * cos(theta);
            const float z = r_at_y * sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(pos);
            glm::vec2 uv(u, v);
            glm::vec3 color(0.8f, 0.8f, 0.8f);

            geometry->AddVertex({ pos, color, uv, normal });
        }
    }

    GenerateGridIndices(geometry.get(), slices, stacks);
    geometry->CreateBuffers();
    return geometry;
}

std::unique_ptr<Geometry> GeometryGenerator::CreatePedestal(VkDevice device, VkPhysicalDevice physicalDevice,
    float topRadius, float baseWidth, float height, int slices, int stacks) {

    auto geometry = std::make_unique<Geometry>(device, physicalDevice);
    geometry->ReserveVertices((slices + 1) * (stacks + 1));
    geometry->ReserveIndices(slices * stacks * 6);

    for (int i = 0; i <= stacks; ++i) {
        const float v = static_cast<float>(i) / stacks;
        const float y = -v * height;

        for (int j = 0; j <= slices; ++j) {
            const float u = static_cast<float>(j) / slices;
            const float theta = 2.0f * static_cast<float>(PI) * u;

            const float x_circle = topRadius * cos(theta);
            const float z_circle = topRadius * sin(theta);

            const float baseRadius = baseWidth * 0.5f;
            const float x_base = baseRadius * cos(theta);
            const float z_base = baseRadius * sin(theta);

            glm::vec3 pos;
            pos.x = glm::mix(x_circle, x_base, v);
            pos.y = y;
            pos.z = glm::mix(z_circle, z_base, v);

            glm::vec2 uv(u, v);
            glm::vec3 color(0.8f, 0.8f, 0.8f);
            glm::vec3 normal(0.0f, 1.0f, 0.0f); // Placeholder

            geometry->AddVertex({ pos, color, uv, normal });
        }
    }

    // Indices
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t current = i * (slices + 1) + j;
            const uint32_t next = current + (slices + 1);

            geometry->AddIndex(current);
            geometry->AddIndex(current + 1);
            geometry->AddIndex(next);

            geometry->AddIndex(current + 1);
            geometry->AddIndex(next + 1);
            geometry->AddIndex(next);
        }
    }

    // Replaced duplicated normal calculation with helper
    ComputeSmoothNormals(geometry.get());

    geometry->CreateBuffers();
    return geometry;
}

std::unique_ptr<Geometry> GeometryGenerator::CreateTerrain(VkDevice device, VkPhysicalDevice physicalDevice,
    float radius, int rings, int segments, float heightScale, float noiseFreq) {

    auto geometry = std::make_unique<Geometry>(device, physicalDevice);
    geometry->ReserveVertices((rings + 1) * (segments + 1));
    geometry->ReserveIndices(rings * segments * 6);

    for (int i = 0; i <= rings; ++i) {
        const float r = static_cast<float>(i) / rings * radius;

        for (int j = 0; j <= segments; ++j) {
            const float theta = static_cast<float>(j) / segments * 2.0f * static_cast<float>(PI);

            const float x = r * cos(theta);
            const float z = r * sin(theta);

            float y = 0.0f;
            y += glm::perlin(glm::vec2(x, z) * noiseFreq);
            y += glm::perlin(glm::vec2(x, z) * noiseFreq * 2.0f) * 0.25f;
            y *= heightScale;

            const float edgeFactor = static_cast<float>(i) / rings;
            if (edgeFactor > 0.9f) {
                const float mask = 1.0f - ((edgeFactor - 0.9f) * 10.0f);
                y *= mask;
            }
            if (i == 0) y = 0.0f;

            glm::vec3 pos(x, y, z);

            const float hFactor = (y / heightScale) + 0.5f;
            const glm::vec3 lowColor = glm::vec3(0.35f, 0.30f, 0.25f);
            const glm::vec3 highColor = glm::vec3(0.45f, 0.40f, 0.30f);
            glm::vec3 color = glm::mix(lowColor, highColor, hFactor);
            if (edgeFactor > 0.9f) color *= (1.0f - ((edgeFactor - 0.9f) * 10.0f));

            glm::vec2 uv;
            uv.x = (x / radius) * 0.5f + 0.5f;
            uv.y = (z / radius) * 0.5f + 0.5f;
            uv *= 80.0f;

            const glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
            geometry->AddVertex({ pos, color, uv, normal });
        }
    }

    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const uint32_t current = i * (segments + 1) + j;
            const uint32_t next = current + (segments + 1);

            geometry->AddIndex(current);
            geometry->AddIndex(current + 1);
            geometry->AddIndex(next);

            geometry->AddIndex(current + 1);
            geometry->AddIndex(next + 1);
            geometry->AddIndex(next);
        }
    }

    // Replaced duplicated normal calculation with helper
    ComputeSmoothNormals(geometry.get());

    geometry->CreateBuffers();
    return geometry;
}

std::unique_ptr<Geometry> GeometryGenerator::CreateCube(VkDevice device, VkPhysicalDevice physicalDevice) {
    auto geometry = std::make_unique<Geometry>(device, physicalDevice);
    geometry->ReserveVertices(24);
    geometry->ReserveIndices(36);

    auto AddFace = [&](glm::vec3 n, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, glm::vec3 p4, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3, glm::vec2 uv4) {
        geometry->AddVertex({ p1, glm::vec3(1.0f), uv1, n });
        geometry->AddVertex({ p2, glm::vec3(1.0f), uv2, n });
        geometry->AddVertex({ p3, glm::vec3(1.0f), uv3, n });
        geometry->AddVertex({ p4, glm::vec3(1.0f), uv4, n });
        };

    AddFace(glm::vec3(0, 0, 1),
        glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(-0.5f, 0.5f, 0.5f),
        { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 });

    AddFace(glm::vec3(0, 0, -1),
        glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(-0.5f, 0.5f, -0.5f), glm::vec3(0.5f, 0.5f, -0.5f),
        { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 });

    AddFace(glm::vec3(0, 1, 0),
        glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(-0.5f, 0.5f, -0.5f),
        { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 });

    AddFace(glm::vec3(0, -1, 0),
        glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(-0.5f, -0.5f, 0.5f),
        { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 });

    AddFace(glm::vec3(1, 0, 0),
        glm::vec3(0.5f, -0.5f, 0.5f), glm::vec3(0.5f, -0.5f, -0.5f), glm::vec3(0.5f, 0.5f, -0.5f), glm::vec3(0.5f, 0.5f, 0.5f),
        { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 });

    AddFace(glm::vec3(-1, 0, 0),
        glm::vec3(-0.5f, -0.5f, -0.5f), glm::vec3(-0.5f, -0.5f, 0.5f), glm::vec3(-0.5f, 0.5f, 0.5f), glm::vec3(-0.5f, 0.5f, -0.5f),
        { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 });

    geometry->SetIndices({
        0, 1, 2, 2, 3, 0,       // Front
        4, 5, 6, 6, 7, 4,       // Back
        8, 9, 10, 10, 11, 8,    // Top
        12, 13, 14, 14, 15, 12, // Bottom
        16, 17, 18, 18, 19, 16, // Right
        20, 21, 22, 22, 23, 20  // Left
        });

    geometry->CreateBuffers();
    return geometry;
}

std::unique_ptr<Geometry> GeometryGenerator::CreateGrid(VkDevice device, VkPhysicalDevice physicalDevice, int rows, int cols, float cellSize) {
    auto geometry = std::make_unique<Geometry>(device, physicalDevice);
    const float width = cols * cellSize;
    const float height = rows * cellSize;
    const float startX = -width / 2.0f;
    const float startY = -height / 2.0f;

    geometry->ReserveVertices((rows + 1) * (cols + 1));
    geometry->ReserveIndices(rows * cols * 6);

    for (int row = 0; row <= rows; ++row) {
        for (int col = 0; col <= cols; ++col) {
            const float x = startX + col * cellSize;
            const float y = startY + row * cellSize;
            glm::vec3 color = GenerateColor(row * (cols + 1) + col, (rows + 1) * (cols + 1));
            glm::vec2 uv = glm::vec2(static_cast<float>(col) / cols, static_cast<float>(row) / rows);
            geometry->AddVertex({ glm::vec3(x, 0.0f, y), color, uv, glm::vec3(0.0f, 1.0f, 0.0f) });
        }
    }
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const uint32_t topLeft = row * (cols + 1) + col;
            const uint32_t topRight = topLeft + 1;
            const uint32_t bottomLeft = (row + 1) * (cols + 1) + col;
            const uint32_t bottomRight = bottomLeft + 1;
            geometry->AddIndex(topLeft);
            geometry->AddIndex(bottomLeft);
            geometry->AddIndex(topRight);
            geometry->AddIndex(topRight);
            geometry->AddIndex(bottomLeft);
            geometry->AddIndex(bottomRight);
        }
    }
    geometry->CreateBuffers();
    return geometry;
}

std::unique_ptr<Geometry> GeometryGenerator::CreateSphere(VkDevice device, VkPhysicalDevice physicalDevice, int stacks, int slices, float radius) {
    if (stacks < 2) stacks = 2;
    if (slices < 3) slices = 3;
    auto geometry = std::make_unique<Geometry>(device, physicalDevice);

    geometry->ReserveVertices((stacks + 1) * (slices + 1));
    geometry->ReserveIndices(stacks * slices * 6);

    for (int i = 0; i <= stacks; ++i) {
        const float phi = static_cast<float>(PI) * i / stacks;
        const float y = radius * cos(phi);
        const float sinPhi = sin(phi);

        for (int j = 0; j <= slices; ++j) {
            const float theta = 2.0f * static_cast<float>(PI) * j / slices;
            const float x = radius * sinPhi * cos(theta);
            const float z = radius * sinPhi * sin(theta);

            glm::vec3 pos = glm::vec3(x, y, z);
            const glm::vec3 normal = glm::normalize(pos);
            glm::vec3 color = GenerateColor(i * (slices + 1) + j, (stacks + 1) * (slices + 1));
            glm::vec2 uv = glm::vec2(static_cast<float>(j) / slices, 1.0f - static_cast<float>(i) / stacks);

            geometry->AddVertex({ pos, color, uv, normal });
        }
    }

    GenerateGridIndices(geometry.get(), slices, stacks);
    geometry->CreateBuffers();
    return geometry;
}

glm::vec3 GeometryGenerator::GenerateColor(int index, int total) {
    const float hue = static_cast<float>(index) / static_cast<float>(total);
    float r, g, b;
    if (hue < 0.33f) { r = 1.0f - (hue / 0.33f); g = hue / 0.33f; b = 0.0f; }
    else if (hue < 0.66f) { r = 0.0f; g = 1.0f - ((hue - 0.33f) / 0.33f); b = (hue - 0.33f) / 0.33f; }
    else { r = (hue - 0.66f) / 0.34f; g = 0.0f; b = 1.0f - ((hue - 0.66f) / 0.34f); }
    return glm::vec3(r, g, b);
}