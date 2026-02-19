#define _CRT_SECURE_NO_WARNINGS

#include "SJGLoader.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <algorithm>

// Helper to replace commas with spaces for easier parsing
std::string ReplaceCommas(std::string str) {
    std::replace(str.begin(), str.end(), ',', ' ');
    return str;
}

std::unique_ptr<Geometry> SJGLoader::Load(VkDevice device, VkPhysicalDevice physicalDevice, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SJG file: " + filepath);
    }

    auto geometry = std::make_unique<Geometry>(device, physicalDevice);
    std::string line;

    // Detect if this is the plane to apply the lighting fix
    bool isPlane = (filepath.find("plane.sjg") != std::string::npos);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        if (line.find("Vertex Format") != std::string::npos) {
            if (!std::getline(file, line)) break;
            int vertexCount = std::stoi(line);

            for (int i = 0; i < vertexCount; ++i) {
                if (!std::getline(file, line)) break;

                // Replace commas with spaces to handle both "x,y,z" and "x y z" formats
                std::string cleanLine = ReplaceCommas(line);

                float x, y, z, nx, ny, nz;
                if (sscanf(cleanLine.c_str(), "%f %f %f %f %f %f", &x, &y, &z, &nx, &ny, &nz) == 6) {
                    Vertex v{};
                    v.pos = glm::vec3(x, y, z);

                    // FIX: Force Plane normals to point perpendicular (Z-axis) 
                    // so when rotated -90 X, they point Up (Y-axis).
                    if (isPlane) {
                        v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                    }
                    else {
                        v.normal = glm::vec3(nx, ny, nz);
                    }

                    v.color = glm::vec3(1.0f); // White
                    // Simple UVs
                    if (isPlane) v.texCoord = glm::vec2(x, y);
                    else v.texCoord = glm::vec2(x, z);

                    vertices.push_back(v);
                }
            }
        }
        else if (line.find("Index Format") != std::string::npos) {
            if (!std::getline(file, line)) break;
            int faceCount = std::stoi(line);

            for (int i = 0; i < faceCount; ++i) {
                if (!std::getline(file, line)) break;

                std::string cleanLine = ReplaceCommas(line);
                int v1, v2, v3;
                if (sscanf(cleanLine.c_str(), "%d %d %d", &v1, &v2, &v3) == 3) {
                    indices.push_back(v1);
                    indices.push_back(v2);
                    indices.push_back(v3);
                }
            }
        }
    }

    if (vertices.empty()) {
        throw std::runtime_error("SJG file contained no valid vertices: " + filepath);
    }

    for (const auto& v : vertices) geometry->AddVertex(v);
    for (const auto& i : indices) geometry->AddIndex(i);

    geometry->CreateBuffers();
    return geometry;
}