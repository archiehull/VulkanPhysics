#pragma once

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <stdexcept>

// ------------------------------------------------------------------
// 1. Define the Entity
// An Entity is just a unique ID. It has no data or logic of its own.
// ------------------------------------------------------------------
using Entity = uint32_t;
const Entity MAX_ENTITIES = 5000;

// ------------------------------------------------------------------
// 2. Component Arrays
// We need a way to store arrays of components. We use an interface 
// so the Registry can hold a list of different ComponentArray types.
// ------------------------------------------------------------------
class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void EntityDestroyed(Entity entity) = 0;
};

template <typename T>
class ComponentArray : public IComponentArray {
private:
    // Maps an Entity ID to its Component data
    std::unordered_map<Entity, T> componentData;

public:
    void InsertData(Entity entity, T component) {
        componentData[entity] = component;
    }

    void RemoveData(Entity entity) {
        componentData.erase(entity);
    }

    T& GetData(Entity entity) {
        if (componentData.find(entity) == componentData.end()) {
            throw std::runtime_error("Retrieving non-existent component.");
        }
        return componentData[entity];
    }

    bool HasData(Entity entity) const {
        return componentData.find(entity) != componentData.end();
    }

    // Called automatically by the Registry when an entity is destroyed
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