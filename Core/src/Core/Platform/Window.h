#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <memory>
#include <string>

namespace core
{
    struct WindowSpec
    {
        std::string title = "IDE";
        int width = 1600;
        int height = 900;
        bool vsync = true;
    };

    /// System window together with the OpenGL context. The application layer doesn't see GLFW.
    class Window : NonCopyable
    {
    public:
        virtual ~Window() = default;

        virtual void PollEvents() = 0;
        virtual void SwapBuffers() = 0;

        [[nodiscard]] virtual bool ShouldClose() const = 0;
        virtual void RequestClose() = 0;

        virtual void FramebufferSize(int& width, int& height) const = 0;
        virtual void SetVSync(bool enabled) = 0;

        /// Platform handle — for the ImGui backend. In practice a GLFWwindow*.
        [[nodiscard]] virtual void* NativeHandle() const = 0;

        static Result<std::unique_ptr<Window>> Create(const WindowSpec& spec);
    };
}
