#include "Window.h"
#include <stdexcept>

Window::Window(uint32_t widthArg, uint32_t heightArg, const std::string& titleArg)
    : window(nullptr), title(titleArg), width(widthArg), height(heightArg) {
    initWindow();
}

Window::~Window() noexcept {
    try {
        Cleanup();
    }
    catch (...) {
        // Ensure destructor does not allow exceptions to propagate.
    }
}

Window::Window(Window&& other) noexcept
    : window(other.window),
    title(std::move(other.title)),
    width(other.width),
    height(other.height) {
    other.window = nullptr;
    other.width = 0;
    other.height = 0;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this == &other) return *this;

    // Release current resources
    Cleanup();

    window = other.window;
    title = std::move(other.title);
    width = other.width;
    height = other.height;

    other.window = nullptr;
    other.width = 0;
    other.height = 0;

    return *this;
}

void Window::initWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("failed to initialize GLFW!");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!window) {
        glfwTerminate();
        throw std::runtime_error("failed to create GLFW window!");
    }
}

void Window::Cleanup() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}