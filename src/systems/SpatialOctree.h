#pragma once
#include <vector>
#include <array>
#include <glm/glm.hpp>
#include "../core/ECS.h"

struct BoidItem {
    Entity id;
    glm::vec3 pos;
};

struct AABB {
    glm::vec3 center;
    glm::vec3 halfExtents;

    bool Contains(const glm::vec3& p) const {
        return (p.x >= center.x - halfExtents.x && p.x <= center.x + halfExtents.x &&
            p.y >= center.y - halfExtents.y && p.y <= center.y + halfExtents.y &&
            p.z >= center.z - halfExtents.z && p.z <= center.z + halfExtents.z);
    }

    bool Intersects(const AABB& other) const {
        if (std::abs(center.x - other.center.x) > (halfExtents.x + other.halfExtents.x)) return false;
        if (std::abs(center.y - other.center.y) > (halfExtents.y + other.halfExtents.y)) return false;
        if (std::abs(center.z - other.center.z) > (halfExtents.z + other.halfExtents.z)) return false;
        return true;
    }
};

struct OctreeNode {
    AABB boundary;
    std::vector<BoidItem> items;
    int children[8];
    bool isLeaf;

    OctreeNode() { Reset(); }
    void Reset() {
        items.clear();
        isLeaf = true;
        for (int i = 0; i < 8; ++i) children[i] = -1;
    }
};

class SpatialOctree {
public:
    SpatialOctree(int capacity = 16) : m_NodeCapacity(capacity) {
        m_Nodes.reserve(1000); // Pre-allocate some nodes to avoid initial reallocation
    }

    void Build(const glm::vec3& boundsMin, const glm::vec3& boundsMax,
        const std::vector<Entity>& entities,
        const std::vector<glm::vec3>& positions) {

        m_ActiveNodes = 1;
        if (m_Nodes.empty()) m_Nodes.emplace_back();

        m_Nodes[0].Reset();
        m_Nodes[0].boundary.center = (boundsMin + boundsMax) * 0.5f;
        m_Nodes[0].boundary.halfExtents = (boundsMax - boundsMin) * 0.5f;

        for (size_t i = 0; i < entities.size(); ++i) {
            Insert(0, { entities[i], positions[i] });
        }
    }

    // Stack-based query (avoids recursion overhead in hot loops)
    void Query(const glm::vec3& pos, float radius, std::vector<Entity>& outNeighbors) const {
        outNeighbors.clear();
        if (m_ActiveNodes == 0) return;

        AABB searchBox{ pos, glm::vec3(radius) };

        // Use a fixed-size stack for tree traversal (max depth of octree usually < 10)
        int stack[64];
        int stackPtr = 0;
        stack[stackPtr++] = 0; // Push root

        float radiusSq = radius * radius;

        while (stackPtr > 0) {
            int nodeIdx = stack[--stackPtr];
            const OctreeNode& node = m_Nodes[nodeIdx];

            if (!node.boundary.Intersects(searchBox)) continue;

            // If it's a leaf, check elements
            if (node.isLeaf) {
                for (const auto& item : node.items) {
                    // We can pre-filter by exact distance here since we cached the position!
                    float distSq = glm::dot(item.pos - pos, item.pos - pos);
                    if (distSq <= radiusSq) {
                        outNeighbors.push_back(item.id);
                    }
                }
            }
            else {
                // Push children to stack
                for (int i = 0; i < 8; ++i) {
                    if (node.children[i] != -1) {
                        stack[stackPtr++] = node.children[i];
                    }
                }
            }
        }
    }

    size_t GetMemoryUsage() const {
        size_t mem = sizeof(SpatialOctree);
        mem += m_Nodes.capacity() * sizeof(OctreeNode);
        for (int i = 0; i < m_ActiveNodes; ++i) {
            mem += m_Nodes[i].items.capacity() * sizeof(BoidItem);
        }
        return mem;
    }

private:
    std::vector<OctreeNode> m_Nodes;
    int m_ActiveNodes = 0;
    int m_NodeCapacity;

    int AllocateNode() {
        if (m_ActiveNodes >= m_Nodes.size()) {
            m_Nodes.emplace_back();
        }
        else {
            m_Nodes[m_ActiveNodes].Reset();
        }
        return m_ActiveNodes++;
    }

    void Subdivide(int nodeIdx) {
        OctreeNode& node = m_Nodes[nodeIdx];
        glm::vec3 c = node.boundary.center;
        glm::vec3 h = node.boundary.halfExtents * 0.5f; // Quarter of total width

        for (int i = 0; i < 8; ++i) {
            int childIdx = AllocateNode();
            // Re-fetch parent node reference (AllocateNode might have caused vector reallocation!)
            OctreeNode& parentNode = m_Nodes[nodeIdx];
            OctreeNode& childNode = m_Nodes[childIdx];

            parentNode.children[i] = childIdx;

            glm::vec3 offset(
                (i & 1) ? h.x : -h.x,
                (i & 2) ? h.y : -h.y,
                (i & 4) ? h.z : -h.z
            );
            childNode.boundary.center = c + offset;
            childNode.boundary.halfExtents = h;
        }

        m_Nodes[nodeIdx].isLeaf = false;
    }

    void Insert(int nodeIdx, const BoidItem& item) {
        // Find the deepest node this item fits in iteratively
        int currentIdx = nodeIdx;

        while (true) {
            OctreeNode& node = m_Nodes[currentIdx];

            if (node.isLeaf) {
                if (node.items.size() < m_NodeCapacity) {
                    node.items.push_back(item);
                    return;
                }
                else {
                    Subdivide(currentIdx);
                    // Re-fetch node after subdivision
                    OctreeNode& subdividedNode = m_Nodes[currentIdx];

                    // Re-distribute existing items
                    for (const auto& existingItem : subdividedNode.items) {
                        for (int i = 0; i < 8; ++i) {
                            if (m_Nodes[subdividedNode.children[i]].boundary.Contains(existingItem.pos)) {
                                Insert(subdividedNode.children[i], existingItem);
                                break;
                            }
                        }
                    }
                    subdividedNode.items.clear();
                    // Loop continues to insert the *new* item into the newly created children
                }
            }
            else {
                bool inserted = false;
                for (int i = 0; i < 8; ++i) {
                    if (m_Nodes[node.children[i]].boundary.Contains(item.pos)) {
                        currentIdx = node.children[i];
                        inserted = true;
                        break;
                    }
                }
                // Fallback: If floating point inaccuracies cause it to miss all children, 
                // force it into child 0 to prevent infinite loops.
                if (!inserted) currentIdx = node.children[0];
            }
        }
    }
};