#pragma once

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <stdexcept>
#include <vector>

using Entity = uint32_t;
const Entity MAX_ENTITIES = 5000;

class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void EntityDestroyed(Entity entity) = 0;
};

// ------------------------------------------------------------------
// OPTIMIZED COMPONENT ARRAY (Sparse Set Pattern)
// ------------------------------------------------------------------
template <typename T>
class ComponentArray : public IComponentArray {
private:
    // The packed array of actual component data (Cache friendly!)
    std::vector<T> componentData;

    // Map from Entity ID to index in componentData (The "Sparse" array concept)
    std::vector<size_t> entityToIndex;

    // Map from index in componentData back to Entity ID (Used for safe deletion)
    std::vector<Entity> indexToEntity;

    size_t validSize = 0; // How many components are currently active

public:
    ComponentArray() {
        // Pre-allocate arrays to maximum entities to avoid runtime reallocations
        componentData.resize(MAX_ENTITIES);
        entityToIndex.resize(MAX_ENTITIES, -1); // -1 means invalid/no component
        indexToEntity.resize(MAX_ENTITIES, -1);
    }

    void InsertData(Entity entity, T component) {
        if (entityToIndex[entity] != static_cast<size_t>(-1)) {
            // Overwrite existing component
            componentData[entityToIndex[entity]] = component;
            return;
        }

        // Put new component at the end of the packed array
        size_t newIndex = validSize;
        entityToIndex[entity] = newIndex;
        indexToEntity[newIndex] = entity;
        componentData[newIndex] = component;
        validSize++;
    }

    void RemoveData(Entity entity) {
        if (entityToIndex[entity] == static_cast<size_t>(-1)) return; // Entity doesn't have this component

        // To keep the dense array packed, we move the LAST element into the deleted element's spot.
        size_t indexOfRemovedEntity = entityToIndex[entity];
        size_t indexOfLastElement = validSize - 1;

        if (indexOfRemovedEntity != indexOfLastElement) {
            // Swap data
            componentData[indexOfRemovedEntity] = componentData[indexOfLastElement];

            // Update the tracking maps for the moved element
            Entity entityOfLastElement = indexToEntity[indexOfLastElement];
            entityToIndex[entityOfLastElement] = indexOfRemovedEntity;
            indexToEntity[indexOfRemovedEntity] = entityOfLastElement;
        }

        // Invalidate the removed entity's tracking
        entityToIndex[entity] = -1;
        indexToEntity[indexOfLastElement] = -1;
        validSize--;
    }

    T& GetData(Entity entity) {
        size_t index = entityToIndex[entity];
        if (index == static_cast<size_t>(-1)) {
            throw std::runtime_error("Retrieving non-existent component.");
        }
        return componentData[index];
    }

    bool HasData(Entity entity) const {
        // Ensure entity is within bounds before checking
        if (entity >= entityToIndex.size()) return false;
        return entityToIndex[entity] != static_cast<size_t>(-1);
    }

    void EntityDestroyed(Entity entity) override {
        if (HasData(entity)) {
            RemoveData(entity);
        }
    }
};

// ------------------------------------------------------------------
// 3. The Registry (The ECS Manager)
// This class manages all entities and component arrays.
// ------------------------------------------------------------------
class Registry {
private:
    Entity nextEntityId = 0;

    // Queue of reusable IDs from destroyed entities
    std::queue<Entity> availableEntities;

    // Map of component types to their respective arrays
    std::unordered_map<std::type_index, std::shared_ptr<IComponentArray>> componentArrays;

    // Helper to get or create a component array for type T
    template<typename T>
    std::shared_ptr<ComponentArray<T>> GetComponentArray() {
        auto type = std::type_index(typeid(T));

        // If the array for this type doesn't exist yet, create it
        if (componentArrays.find(type) == componentArrays.end()) {
            componentArrays[type] = std::make_shared<ComponentArray<T>>();
        }

        return std::static_pointer_cast<ComponentArray<T>>(componentArrays[type]);
    }

public:
    // --- Entity Management ---

    Entity CreateEntity() {
        if (!availableEntities.empty()) {
            Entity id = availableEntities.front();
            availableEntities.pop();
            return id;
        }

        if (nextEntityId >= MAX_ENTITIES) {
            throw std::runtime_error("Too many entities in existence.");
        }

        return nextEntityId++;
    }

    void DestroyEntity(Entity entity) {
        // Remove this entity's components from ALL arrays
        for (auto const& pair : componentArrays) {
            pair.second->EntityDestroyed(entity);
        }

        // Make the ID available for reuse
        availableEntities.push(entity);
    }

    // --- Component Management ---

    template <typename T>
    void AddComponent(Entity entity, T component) {
        GetComponentArray<T>()->InsertData(entity, component);
    }

    template <typename T>
    void RemoveComponent(Entity entity) {
        GetComponentArray<T>()->RemoveData(entity);
    }

    template <typename T>
    T& GetComponent(Entity entity) {
        return GetComponentArray<T>()->GetData(entity);
    }

    template <typename T>
    bool HasComponent(Entity entity) {
        return GetComponentArray<T>()->HasData(entity);
    }

    // Optional helper to get the highest entity ID currently tracked
    // Useful for simple system iteration loops later.
    Entity GetEntityCount() const {
        return nextEntityId;
    }
};