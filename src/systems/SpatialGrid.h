#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "../core/ECS.h"

class UniformGrid3D {
public:
    void Build(float cellSize, const glm::vec3& boundsMin, const glm::vec3& boundsMax,
        const std::vector<Entity>& entities,
        const std::vector<glm::vec3>& positions) {

        m_CellSize = std::max(cellSize, 0.1f);
        m_BoundsMin = boundsMin;

        glm::vec3 size = boundsMax - boundsMin;
        m_GridDims = glm::ivec3(
            std::ceil(size.x / m_CellSize),
            std::ceil(size.y / m_CellSize),
            std::ceil(size.z / m_CellSize)
        );

        // Ensure minimum dims
        m_GridDims = glm::max(m_GridDims, glm::ivec3(1));

        size_t totalCells = m_GridDims.x * m_GridDims.y * m_GridDims.z;
        m_Cells.assign(totalCells, std::vector<Entity>()); // Clear and resize

        // Insert entities
        for (size_t i = 0; i < entities.size(); ++i) {
            int index = GetCellIndex(positions[i]);
            if (index >= 0 && index < m_Cells.size()) {
                m_Cells[index].push_back(entities[i]);
            }
        }
    }

    // Thread-safe query (const)
    void Query(const glm::vec3& pos, float radius, std::vector<Entity>& outNeighbors) const {
        outNeighbors.clear();

        glm::ivec3 centerCell = GetCellCoords(pos);
        int searchRadius = std::ceil(radius / m_CellSize);

        // Search neighboring cells
        for (int z = -searchRadius; z <= searchRadius; ++z) {
            for (int y = -searchRadius; y <= searchRadius; ++y) {
                for (int x = -searchRadius; x <= searchRadius; ++x) {
                    glm::ivec3 target(centerCell.x + x, centerCell.y + y, centerCell.z + z);

                    // Wrap-around logic (optional, based on your bounds logic)
                    target.x = (target.x % m_GridDims.x + m_GridDims.x) % m_GridDims.x;
                    target.y = (target.y % m_GridDims.y + m_GridDims.y) % m_GridDims.y;
                    target.z = (target.z % m_GridDims.z + m_GridDims.z) % m_GridDims.z;

                    int index = Get1DIndex(target);
                    if (index >= 0 && index < m_Cells.size()) {
                        outNeighbors.insert(outNeighbors.end(), m_Cells[index].begin(), m_Cells[index].end());
                    }
                }
            }
        }
    }

    size_t GetMemoryUsage() const {
        size_t mem = sizeof(UniformGrid3D);
        mem += m_Cells.capacity() * sizeof(std::vector<Entity>);
        for (const auto& cell : m_Cells) {
            mem += cell.capacity() * sizeof(Entity);
        }
        return mem;
    }

private:
    float m_CellSize;
    glm::vec3 m_BoundsMin;
    glm::ivec3 m_GridDims;
    std::vector<std::vector<Entity>> m_Cells;

    glm::ivec3 GetCellCoords(const glm::vec3& pos) const {
        glm::vec3 localPos = pos - m_BoundsMin;
        return glm::ivec3(
            std::floor(localPos.x / m_CellSize),
            std::floor(localPos.y / m_CellSize),
            std::floor(localPos.z / m_CellSize)
        );
    }

    int Get1DIndex(const glm::ivec3& coords) const {
        return coords.x + m_GridDims.x * (coords.y + m_GridDims.y * coords.z);
    }

    int GetCellIndex(const glm::vec3& pos) const {
        return Get1DIndex(GetCellCoords(pos));
    }
};