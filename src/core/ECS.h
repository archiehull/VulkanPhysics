#pragma once

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <stdexcept>
#include <vector>
#include <limits>
#include <iostream>

using Entity = uint32_t;
const Entity MAX_ENTITIES = 30000;

class IComponentArray {
public:
    virtual ~IComponentArray() = default;
    virtual void EntityDestroyed(Entity entity) = 0;
};

template <typename T>
class ComponentArray : public IComponentArray {
private:
    std::vector<T> componentData;
    std::vector<size_t> entityToIndex;
    std::vector<Entity> indexToEntity;
    size_t validSize = 0;
    static constexpr size_t INVALID_INDEX = static_cast<size_t>(-1);

public:
    ComponentArray() {
        componentData.resize(MAX_ENTITIES);
        entityToIndex.resize(MAX_ENTITIES, INVALID_INDEX);
        indexToEntity.resize(MAX_ENTITIES, static_cast<Entity>(-1));
    }

    void InsertData(Entity entity, T component) {
        if (entity >= MAX_ENTITIES) {
            std::cerr << "[ComponentArray] InsertData: entity index out of range: " << entity << std::endl;
            return;
        }
        if (entityToIndex[entity] != INVALID_INDEX) {
            componentData[entityToIndex[entity]] = component;
            return;
        }
        size_t newIndex = validSize;
        entityToIndex[entity] = newIndex;
        indexToEntity[newIndex] = entity;
        componentData[newIndex] = component;
        validSize++;
    }

    void RemoveData(Entity entity) {
        if (entity >= entityToIndex.size()) {
            std::cerr << "[ComponentArray] RemoveData: entity >= capacity: " << entity << std::endl;
            return;
        }
        if (entityToIndex[entity] == INVALID_INDEX) return;

        if (validSize == 0) {
            // Corruption guard: shouldn't happen, but avoid underflow
            std::cerr << "[ComponentArray] RemoveData: validSize==0 but entity had component. Resetting entry for entity " << entity << std::endl;
            entityToIndex[entity] = INVALID_INDEX;
            return;
        }

        size_t indexOfRemovedEntity = entityToIndex[entity];
        size_t indexOfLastElement = validSize - 1;

        if (indexOfRemovedEntity != indexOfLastElement) {
            componentData[indexOfRemovedEntity] = componentData[indexOfLastElement];
            Entity entityOfLastElement = indexToEntity[indexOfLastElement];
            if (entityOfLastElement < entityToIndex.size()) {
                entityToIndex[entityOfLastElement] = indexOfRemovedEntity;
            }
            else {
                std::cerr << "[ComponentArray] RemoveData: entityOfLastElement out of range: " << entityOfLastElement << std::endl;
            }
            indexToEntity[indexOfRemovedEntity] = entityOfLastElement;
        }

        entityToIndex[entity] = INVALID_INDEX;
        indexToEntity[indexOfLastElement] = static_cast<Entity>(-1);
        validSize--;
    }

    T& GetData(Entity entity) {
        if (entity >= entityToIndex.size()) {
            throw std::runtime_error("Retrieving non-existent component: entity out of range.");
        }
        size_t index = entityToIndex[entity];
        if (index == INVALID_INDEX) {
            throw std::runtime_error("Retrieving non-existent component.");
        }
        return componentData[index];
    }

    // --- ADD CONST OVERLOAD ---
    const T& GetData(Entity entity) const {
        if (entity >= entityToIndex.size()) {
            throw std::runtime_error("Retrieving non-existent component: entity out of range.");
        }
        size_t index = entityToIndex[entity];
        if (index == INVALID_INDEX) {
            throw std::runtime_error("Retrieving non-existent component.");
        }
        return componentData[index];
    }

    bool HasData(Entity entity) const {
        if (entity >= entityToIndex.size()) return false;
        return entityToIndex[entity] != INVALID_INDEX;
    }

    void EntityDestroyed(Entity entity) override {
        if (HasData(entity)) {
            RemoveData(entity);
        }
    }

    size_t GetSize() const { return validSize; }
    Entity GetEntityAtIndex(size_t index) const {
        if (index >= validSize) return MAX_ENTITIES;
        return indexToEntity[index];
    }
};

class Registry {
private:
    Entity nextEntityId = 0;
    std::queue<Entity> availableEntities;
    std::vector<bool> isAlive;  // Track which entities are currently alive to prevent double-free
    std::unordered_map<std::type_index, std::shared_ptr<IComponentArray>> componentArrays;



public:
    template<typename T>
    std::shared_ptr<ComponentArray<T>> GetComponentArray() {
        auto type = std::type_index(typeid(T));
        if (componentArrays.find(type) == componentArrays.end()) {
            componentArrays[type] = std::make_shared<ComponentArray<T>>();
        }
        return std::static_pointer_cast<ComponentArray<T>>(componentArrays[type]);
    }

    // --- ADD CONST VERSION (No lazy creation) ---
    template<typename T>
    std::shared_ptr<const ComponentArray<T>> GetComponentArray() const {
        auto type = std::type_index(typeid(T));
        auto it = componentArrays.find(type);
        if (it == componentArrays.end()) return nullptr;
        return std::static_pointer_cast<const ComponentArray<T>>(it->second);
    }

    bool IsAlive(Entity entity) const {
        if (entity >= isAlive.size()) return false;
        return isAlive[entity];
    }

    Entity CreateEntity() {
        if (!availableEntities.empty()) {
            Entity id = availableEntities.front();
            availableEntities.pop();
            if (id >= isAlive.size()) {
                isAlive.resize(id + 1, false);
            }
            isAlive[id] = true;
            return id;
        }
        if (nextEntityId >= MAX_ENTITIES) {
            throw std::runtime_error("Too many entities.");
        }
        Entity id = nextEntityId++;
        if (id >= isAlive.size()) {
            isAlive.resize(id + 1, false);
        }
        isAlive[id] = true;
        return id;
    }

    void DestroyEntity(Entity entity) {
        // Prevent double-free: if entity is already dead, ignore
        if (entity >= isAlive.size() || !isAlive[entity]) {
            return;
        }
        isAlive[entity] = false;
        for (auto const& pair : componentArrays) {
            pair.second->EntityDestroyed(entity);
        }
        availableEntities.push(entity);
    }

    void Clear() {
        for (auto const& pair : componentArrays) {
            // We can't easily clear the vectors inside without more template logic, 
            // but we can at least invalidate all entities.
            for (Entity i = 0; i < nextEntityId; ++i) {
                if (isAlive[i]) pair.second->EntityDestroyed(i);
            }
        }
        nextEntityId = 0;
        while (!availableEntities.empty()) availableEntities.pop();
        std::fill(isAlive.begin(), isAlive.end(), false);
    }

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

    // --- ADD CONST OVERLOAD ---
    template <typename T>
    const T& GetComponent(Entity entity) const {
        auto array = GetComponentArray<T>();
        if (!array) throw std::runtime_error("Component array does not exist.");
        return array->GetData(entity);
    }

    template <typename T>
    bool HasComponent(Entity entity) {
        return GetComponentArray<T>()->HasData(entity);
    }

    // --- ADD CONST OVERLOAD ---
    template <typename T>
    bool HasComponent(Entity entity) const {
        auto array = GetComponentArray<T>();
        return array && array->HasData(entity);
    }

    Entity GetEntityCount() const {
        return nextEntityId;
    }
};