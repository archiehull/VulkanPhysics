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

template <typename T>
class ComponentArray : public IComponentArray {
private:
    std::vector<T> componentData;
    std::vector<size_t> entityToIndex;
    std::vector<Entity> indexToEntity;
    size_t validSize = 0;

public:
    ComponentArray() {
        componentData.resize(MAX_ENTITIES);
        entityToIndex.resize(MAX_ENTITIES, -1);
        indexToEntity.resize(MAX_ENTITIES, -1);
    }

    void InsertData(Entity entity, T component) {
        if (entityToIndex[entity] != static_cast<size_t>(-1)) {
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
        if (entityToIndex[entity] == static_cast<size_t>(-1)) return;

        size_t indexOfRemovedEntity = entityToIndex[entity];
        size_t indexOfLastElement = validSize - 1;

        if (indexOfRemovedEntity != indexOfLastElement) {
            componentData[indexOfRemovedEntity] = componentData[indexOfLastElement];
            Entity entityOfLastElement = indexToEntity[indexOfLastElement];
            entityToIndex[entityOfLastElement] = indexOfRemovedEntity;
            indexToEntity[indexOfRemovedEntity] = entityOfLastElement;
        }

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

    // --- ADD CONST OVERLOAD ---
    const T& GetData(Entity entity) const {
        size_t index = entityToIndex[entity];
        if (index == static_cast<size_t>(-1)) {
            throw std::runtime_error("Retrieving non-existent component.");
        }
        return componentData[index];
    }

    bool HasData(Entity entity) const {
        if (entity >= entityToIndex.size()) return false;
        return entityToIndex[entity] != static_cast<size_t>(-1);
    }

    void EntityDestroyed(Entity entity) override {
        if (HasData(entity)) {
            RemoveData(entity);
        }
    }
};

class Registry {
private:
    Entity nextEntityId = 0;
    std::queue<Entity> availableEntities;
    std::unordered_map<std::type_index, std::shared_ptr<IComponentArray>> componentArrays;

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

public:
    Entity CreateEntity() {
        if (!availableEntities.empty()) {
            Entity id = availableEntities.front();
            availableEntities.pop();
            return id;
        }
        if (nextEntityId >= MAX_ENTITIES) {
            throw std::runtime_error("Too many entities.");
        }
        return nextEntityId++;
    }

    void DestroyEntity(Entity entity) {
        for (auto const& pair : componentArrays) {
            pair.second->EntityDestroyed(entity);
        }
        availableEntities.push(entity);
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