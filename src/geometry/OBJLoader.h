#pragma once

#include "Geometry.h"
#include <string>
#include <memory>
#include <vulkan/vulkan.h>

class OBJLoader final {
public:
    // Static-only utility
    OBJLoader() = delete;
    ~OBJLoader() = delete;

    OBJLoader(const OBJLoader&) = delete;
    OBJLoader& operator=(const OBJLoader&) = delete;
    OBJLoader(OBJLoader&&) = delete;
    OBJLoader& operator=(OBJLoader&&) = delete;

    static std::unique_ptr<Geometry> Load(VkDevice device, VkPhysicalDevice physicalDevice, const std::string& filepath);
};