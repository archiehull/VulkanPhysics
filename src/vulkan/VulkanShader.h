#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <string>

class VulkanShader final {
public:
    explicit VulkanShader(VkDevice device);
    ~VulkanShader() = default;

    VulkanShader(const VulkanShader&) = delete;
    VulkanShader& operator=(const VulkanShader&) = delete;

    void LoadShader(const std::string& filename, VkShaderStageFlagBits stage);
    void Cleanup() const;

    VkShaderModule GetVertexShader() const { return vertexShaderModule; }
    VkShaderModule GetFragmentShader() const { return fragmentShaderModule; }

private:
    VkDevice device;
    VkShaderModule vertexShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragmentShaderModule = VK_NULL_HANDLE;

    VkShaderModule createShaderModule(const std::vector<char>& code) const;
    static void readFile(const std::string& filename, std::vector<char>& output);
};