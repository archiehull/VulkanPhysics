#include "VulkanShader.h"
#include <fstream>
#include <stdexcept>

VulkanShader::VulkanShader(VkDevice deviceArg) : device(deviceArg) {
}

void VulkanShader::LoadShader(const std::string& filename, VkShaderStageFlagBits stage) {
    std::vector<char> code;
    readFile(filename, code);

    const VkShaderModule shaderModule = createShaderModule(code);

    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        vertexShaderModule = shaderModule;
    }
    else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        fragmentShaderModule = shaderModule;
    }
}

VkShaderModule VulkanShader::createShaderModule(const std::vector<char>& code) const {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

void VulkanShader::readFile(const std::string& filename, std::vector<char>& output) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    output.resize(fileSize);

    file.seekg(0);
    file.read(output.data(), fileSize);
    file.close();
}

void VulkanShader::Cleanup() const {
    if (fragmentShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, fragmentShaderModule, nullptr);
    }
    if (vertexShaderModule != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vertexShaderModule, nullptr);
    }
}