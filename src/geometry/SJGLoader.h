#pragma once

#include "Geometry.h"
#include <string>
#include <memory>
#include <vulkan/vulkan.h>

class SJGLoader final {
public:
    // Static-only utility
    SJGLoader() = delete;
    ~SJGLoader() = delete;

    SJGLoader(const SJGLoader&) = delete;
    SJGLoader& operator=(const SJGLoader&) = delete;
    SJGLoader(SJGLoader&&) = delete;
    SJGLoader& operator=(SJGLoader&&) = delete;

    static std::unique_ptr<Geometry> Load(VkDevice device, VkPhysicalDevice physicalDevice, const std::string& filepath);
};