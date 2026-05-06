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
#include <mutex>

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
    std::atomic<Entity> nextEntityId{ 0 }; // Global high-water mark for system loops
    Entity localAllocId{ 0 };              // Local allocator bound to this peer's partition
    std::queue<Entity> availableEntities;
    std::vector<bool> isAlive;  // Track which entities are currently alive to prevent double-free
    std::unordered_map<std::type_index, std::shared_ptr<IComponentArray>> componentArrays;
    mutable std::mutex componentArraysMutex;
    mutable std::mutex entityMutex;

    Entity partitionMin = 0;
    Entity partitionMax = 9999; // Default to the static scene loading

public:
    template<typename T>
    std::shared_ptr<ComponentArray<T>> GetComponentArray() {
        auto type = std::type_index(typeid(T));
        std::lock_guard<std::mutex> lock(componentArraysMutex);
        auto it = componentArrays.find(type);
        if (it == componentArrays.end()) {
            auto arr = std::make_shared<ComponentArray<T>>();
            componentArrays[type] = arr;
            return std::static_pointer_cast<ComponentArray<T>>(componentArrays[type]);
        }
        return std::static_pointer_cast<ComponentArray<T>>(it->second);
    }

    // --- ADD CONST VERSION (No lazy creation) ---
    template<typename T>
    std::shared_ptr<const ComponentArray<T>> GetComponentArray() const {
        auto type = std::type_index(typeid(T));
        std::lock_guard<std::mutex> lock(componentArraysMutex);
        auto it = componentArrays.find(type);
        if (it == componentArrays.end()) return nullptr;
        return std::static_pointer_cast<const ComponentArray<T>>(it->second);
    }

    bool IsAlive(Entity entity) const {
        if (entity >= isAlive.size()) return false;
        return isAlive[entity];
    }

    void SetNetworkPartition(int peerId) {
        if (peerId < 0 || peerId > 3) return;
        std::lock_guard<std::mutex> lock(entityMutex);
        
        partitionMin = peerId * 10000;
        partitionMax = partitionMin + 9999;
        
        // Fast-forward our local ID generator to our new block
        if (localAllocId < partitionMin) {
            localAllocId = partitionMin;
        }
        if (nextEntityId.load() < localAllocId) {
            nextEntityId.store(localAllocId);
        }
        
        std::queue<Entity> emptyQueue;
        std::swap(availableEntities, emptyQueue);
    }

    Entity CreateEntity() {
        std::lock_guard<std::mutex> lock(entityMutex);
        if (!availableEntities.empty()) {
            Entity id = availableEntities.front();
            availableEntities.pop();
            if (id >= isAlive.size()) isAlive.resize(id + 1, false);
            isAlive[id] = true;
            if (id >= nextEntityId.load()) nextEntityId.store(id + 1);
            return id;
        }

        if (localAllocId > partitionMax || localAllocId >= MAX_ENTITIES) {
            throw std::runtime_error("Entity partition exhausted or MAX_ENTITIES reached.");
        }

        Entity id = localAllocId++;
        if (id >= isAlive.size()) isAlive.resize(id + 1, false);
        isAlive[id] = true;
        
        if (id >= nextEntityId.load()) nextEntityId.store(id + 1);
        
        return id;
    }

    void CreateEntityExplicit(Entity id) {
        std::lock_guard<std::mutex> lock(entityMutex);
        if (id >= MAX_ENTITIES) {
            throw std::runtime_error("Entity ID out of range.");
        }
        if (id >= isAlive.size()) {
            isAlive.resize(id + 1, false);
        }
        if (isAlive[id]) return; 

        isAlive[id] = true;
        
        // Advance the global loop bound
        if (id >= nextEntityId.load()) {
            nextEntityId.store(id + 1);
        }

        // ONLY advance the local generator if the explicit ID falls in OUR partition.
        // This stops remote spawns from corrupting our local allocator sequence.
        if (id >= partitionMin && id <= partitionMax) {
            if (id >= localAllocId) localAllocId = id + 1;
        } else if (id < 10000) { 
            if (id >= localAllocId && localAllocId < partitionMin) localAllocId = id + 1;
        }

        std::queue<Entity> temp;
        while (!availableEntities.empty()) {
            Entity top = availableEntities.front();
            availableEntities.pop();
            if (top != id) temp.push(top);
        }
        availableEntities = std::move(temp);
    }

    void DestroyEntity(Entity entity) {
        {
            std::lock_guard<std::mutex> entLock(entityMutex);
            if (entity >= isAlive.size() || !isAlive[entity]) return;
            isAlive[entity] = false;
            
            // Only recycle IDs that belong to OUR partition
            if (entity >= partitionMin && entity <= partitionMax) {
                availableEntities.push(entity);
            }
        }

        std::vector<std::shared_ptr<IComponentArray>> arrays;
        {
            std::lock_guard<std::mutex> lock(componentArraysMutex);
            arrays.reserve(componentArrays.size());
            for (auto const& pair : componentArrays) arrays.push_back(pair.second);
        }
        for (auto const& arr : arrays) {
            if (arr) arr->EntityDestroyed(entity);
        }
    }

    void Clear() {
        std::vector<std::shared_ptr<IComponentArray>> arrays;
        {
            std::lock_guard<std::mutex> lock(componentArraysMutex);
            arrays.reserve(componentArrays.size());
            for (auto const& pair : componentArrays) arrays.push_back(pair.second);
        }

        Entity currentMax = nextEntityId.load();
        for (Entity i = 0; i < currentMax; ++i) {
            if (i < isAlive.size() && isAlive[i]) {
                for (auto const& arr : arrays) {
                    if (arr) arr->EntityDestroyed(i);
                }
            }
        }

        {
            std::lock_guard<std::mutex> entLock(entityMutex);
            nextEntityId.store(0);
            localAllocId = 0;
            while (!availableEntities.empty()) availableEntities.pop();
            std::fill(isAlive.begin(), isAlive.end(), false);
        }
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
        // nextEntityId is atomic, so we can safely read it without the entityMutex.
        // This avoids millions of locks per second in high-frequency system loops.
        return nextEntityId.load();
    }
};