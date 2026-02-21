#pragma once

#include <glm/glm.hpp>
#include <string>
#include <memory>
#include "../geometry/Geometry.h"
#include "ECS.h"
#include "CoreTypes.h"

// 1. Identification
struct NameComponent {
    std::string name;
};

// 2. Spatial Data
struct TransformComponent {
    glm::mat4 matrix = glm::mat4(1.0f);
};

// 3. Visual Data
struct RenderComponent {
    std::shared_ptr<Geometry> geometry;
    std::string texturePath;
    std::string originalTexturePath;

    Entity simpleShadowEntity = MAX_ENTITIES;

    int shadingMode = 1;
    bool visible = true;
    bool castsShadow = true;
    bool receiveShadows = true;
    int layerMask = SceneLayers::INSIDE;
};

// 4. Movement/Logic Data
struct OrbitComponent {
    bool isOrbiting = false;
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 1.0f;
    float speed = 1.0f;
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 startVector = glm::vec3(1.0f, 0.0f, 0.0f);
    float initialAngle = 0.0f;
    float currentAngle = 0.0f;
};

// 5. Fire/Thermodynamics State
struct ThermoComponent {
    ObjectState state = ObjectState::NORMAL;
    bool isFlammable = false;

    float currentTemp = 20.0f;
    float ignitionThreshold = 100.0f;
    float thermalResponse = 5.0f;
    float selfHeatingRate = 15.0f;

    float burnTimer = 0.0f;
    float maxBurnDuration = 10.0f;
    float regrowTimer = 0.0f;
    float burnFactor = 0.0f;

    int fireEmitterId = -1;
    int smokeEmitterId = -1;
    int fireLightEntity = -1;

    std::shared_ptr<Geometry> storedOriginalGeometry = nullptr;
    glm::mat4 storedOriginalTransform = glm::mat4(1.0f);
};

// 6. Physics
struct ColliderComponent {
    bool hasCollision = true;
    float radius = 2.0f;
    float height = 5.0f;
};

// 7. Light
struct LightComponent {
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    int type = 0;
};