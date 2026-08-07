#include "corepch.h"

#include "Core/Platform/GlfwWindow.h"

#include "Core/Base/Log.h"

#include <GLFW/glfw3.h>

namespace core
{
    namespace
    {
        int s_glfwWindowCount = 0;

        void OnGlfwError(int code, const char* description)
        {
            CORE_ERROR("GLFW ({}): {}", code, description);
        }
    }

    GlfwWindow::GlfwWindow(GLFWwindow* handle) : m_handle(handle) {}

    GlfwWindow::~GlfwWindow()
    {
        if (m_handle == nullptr)
            return;

        glfwDestroyWindow(m_handle);
        m_handle = nullptr;

        if (--s_glfwWindowCount == 0)
            glfwTerminate();
    }

    void GlfwWindow::PollEvents()
    {
        glfwPollEvents();
    }

    void GlfwWindow::SwapBuffers()
    {
        glfwSwapBuffers(m_handle);
    }

    bool GlfwWindow::ShouldClose() const
    {
        return glfwWindowShouldClose(m_handle) != 0;
    }

    void GlfwWindow::RequestClose()
    {
        glfwSetWindowShouldClose(m_handle, GLFW_TRUE);
    }

    bool GlfwWindow::IsFocused() const
    {
        return glfwGetWindowAttrib(m_handle, GLFW_FOCUSED) != 0;
    }

    void GlfwWindow::FramebufferSize(int& width, int& height) const
    {
        glfwGetFramebufferSize(m_handle, &width, &height);
    }

    void GlfwWindow::SetVSync(bool enabled)
    {
        glfwMakeContextCurrent(m_handle);
        glfwSwapInterval(enabled ? 1 : 0);
    }

    Result<std::unique_ptr<Window>> Window::Create(const WindowSpec& spec)
    {
        if (s_glfwWindowCount == 0)
        {
            glfwSetErrorCallback(&OnGlfwError);

            if (glfwInit() == GLFW_FALSE)
                return Error{ErrorCode::Unsupported, "glfwInit failed"};
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

        GLFWwindow* handle =
            glfwCreateWindow(spec.width, spec.height, spec.title.c_str(), nullptr, nullptr);

        if (handle == nullptr)
        {
            if (s_glfwWindowCount == 0)
                glfwTerminate();

            return Error{ErrorCode::Unsupported, "failed to create an OpenGL 3.3 window"};
        }

        ++s_glfwWindowCount;

        auto window = std::make_unique<GlfwWindow>(handle);
        glfwMakeContextCurrent(handle);
        window->SetVSync(spec.vsync);

        CORE_INFO("Window created: {}x{} \"{}\"", spec.width, spec.height, spec.title);
        return Result<std::unique_ptr<Window>>{std::move(window)};
    }
}
