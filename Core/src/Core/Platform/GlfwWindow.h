#pragma once

#include "Core/Platform/Window.h"

struct GLFWwindow;

namespace core
{
    class GlfwWindow final : public Window
    {
    public:
        explicit GlfwWindow(GLFWwindow* handle);
        ~GlfwWindow() override;

        void PollEvents() override;
        void SwapBuffers() override;

        [[nodiscard]] bool ShouldClose() const override;
        void RequestClose() override;

        void FramebufferSize(int& width, int& height) const override;
        void SetVSync(bool enabled) override;

        [[nodiscard]] void* NativeHandle() const override { return m_handle; }

    private:
        GLFWwindow* m_handle = nullptr;
    };
}
