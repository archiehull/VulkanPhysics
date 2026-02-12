#include "OBJLoader.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <array>

namespace {
    struct VertexKey {
        int v_idx = -1;
        int vt_idx = -1;
        int vn_idx = -1;
    };

    bool operator==(const VertexKey& a, const VertexKey& b) {
        return a.v_idx == b.v_idx && a.vt_idx == b.vt_idx && a.vn_idx == b.vn_idx;
    }

    struct VertexKeyHash {
        size_t operator()(const VertexKey& k) const;
    };

    size_t VertexKeyHash::operator()(const VertexKey& k) const {
        const size_t h1 = std::hash<int>{}(k.v_idx);
        const size_t h2 = std::hash<int>{}(k.vt_idx);
        const size_t h3 = std::hash<int>{}(k.vn_idx);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
} // namespace

std::unique_ptr<Geometry> OBJLoader::Load(VkDevice device, VkPhysicalDevice physicalDevice, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open OBJ file: " + filepath);
    }

    // Temporary storage for raw file data
    std::vector<glm::vec3> temp_positions;
    std::vector<glm::vec2> temp_texCoords;
    std::vector<glm::vec3> temp_normals;

    // Deduplication map: Key -> Index in the final geometry vertex array
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVertices;

    auto geometry = std::make_unique<Geometry>(device, physicalDevice);

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v") {
            glm::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            temp_positions.push_back(pos);
        }
        else if (prefix == "vt") {
            glm::vec2 uv;
            ss >> uv.x >> uv.y;
            // Vulkan UV coordinate system has (0,0) at top-left.
            // OBJ (0,0) is bottom-left. Flip V.
            uv.y = 1.0f - uv.y;
            temp_texCoords.push_back(uv);
        }
        else if (prefix == "vn") {
            glm::vec3 norm;
            ss >> norm.x >> norm.y >> norm.z;
            temp_normals.push_back(norm);
        }
        else if (prefix == "f") {
            std::vector<VertexKey> faceVertices;
            std::string segment;

            // Parse all vertices in the face (supports triangles and quads)
            while (ss >> segment) {
                VertexKey key;
                std::stringstream segmentSS(segment);
                std::string valStr;

                // 1. Position Index
                if (std::getline(segmentSS, valStr, '/')) {
                    if (!valStr.empty()) key.v_idx = std::stoi(valStr) - 1; // OBJ is 1-based
                }

                // 2. Texture Index
                if (std::getline(segmentSS, valStr, '/')) {
                    if (!valStr.empty()) key.vt_idx = std::stoi(valStr) - 1;
                }

                // 3. Normal Index
                if (std::getline(segmentSS, valStr, '/')) {
                    if (!valStr.empty()) key.vn_idx = std::stoi(valStr) - 1;
                }

                faceVertices.push_back(key);
            }

            // Triangulate (Fan triangulation: 0-1-2, 0-2-3, etc.)
            for (size_t i = 1; i < faceVertices.size() - 1; ++i) {
                std::array<VertexKey, 3> keys = { faceVertices[0], faceVertices[i], faceVertices[i + 1] };

                for (const auto& vk : keys) {
                    const auto it = uniqueVertices.find(vk);
                    if (it == uniqueVertices.end()) {
                        const uint32_t newIndex = static_cast<uint32_t>(geometry->VertexCount());
                        uniqueVertices.emplace(vk, newIndex);

                        Vertex newVertex{};

                        // Set Position
                        if (vk.v_idx >= 0 && vk.v_idx < static_cast<int>(temp_positions.size())) {
                            newVertex.pos = temp_positions[vk.v_idx];
                        }

                        // Set TexCoord (default 0,0 if missing)
                        if (vk.vt_idx >= 0 && vk.vt_idx < static_cast<int>(temp_texCoords.size())) {
                            newVertex.texCoord = temp_texCoords[vk.vt_idx];
                        }

                        // Set Normal (default up vector if missing)
                        if (vk.vn_idx >= 0 && vk.vn_idx < static_cast<int>(temp_normals.size())) {
                            newVertex.normal = temp_normals[vk.vn_idx];
                        }
                        else {
                            newVertex.normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default normal
                        }

                        // Set Default Color (White) since OBJ usually doesn't provide it per vertex
                        newVertex.color = glm::vec3(1.0f, 1.0f, 1.0f);

                        // FIX: Use AddVertex/AddIndex
                        geometry->AddVertex(newVertex);
                        geometry->AddIndex(newIndex);
                    }
                    else {
                        // FIX: Use AddIndex
                        geometry->AddIndex(it->second);
                    }
                }
            }
        }
    }

    if (geometry->VertexCount() == 0) {
        throw std::runtime_error("OBJ file contained no vertices or failed to parse: " + filepath);
    }

    geometry->CreateBuffers();
    return geometry;
}