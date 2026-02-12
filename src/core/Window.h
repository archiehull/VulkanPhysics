#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>

class Window final {
public:
    Window(uint32_t widthArg, uint32_t heightArg, const std::string& titleArg);
    ~Window() noexcept;

    // Non-copyable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Movable
    Window(Window&&) noexcept;
    Window& operator=(Window&&) noexcept;

    // Small accessors/inlines to avoid non-inlined warnings
    bool ShouldClose() const { return glfwWindowShouldClose(window); }
    void PollEvents() const { glfwPollEvents(); }
    GLFWwindow* GetGLFWWindow() const { return window; }

    // Setting the framebuffer callback does not modify observable state in this class
    void SetFramebufferResizeCallback(GLFWframebuffersizefun callback) const { glfwSetFramebufferSizeCallback(window, callback); }

private:
    void initWindow();
    void Cleanup();

    // Reordered members to reduce padding and improve layout
    GLFWwindow* window = nullptr;
    std::string title;
    uint32_t width = 0;
    uint32_t height = 0;
};