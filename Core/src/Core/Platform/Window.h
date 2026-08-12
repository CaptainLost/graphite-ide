#pragma once

#include "Core/Base/Color.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/Caption.h"

#include <memory>
#include <string>

namespace Core
{
    struct WindowSpec
    {
        std::string title = "IDE";
        int width = 1600;
        int height = 900;
        bool vsync = true;

        /// Ask the platform to remove the system title bar so the application can draw
        /// its own. A platform that cannot honour it leaves the native bar in place —
        /// see Window::HasCustomTitleBar.
        bool customTitleBar = false;

        /// The colour the system outlines the window with once the caption is gone. Only
        /// read when customTitleBar is honoured, and passed in rather than chosen here
        /// because it belongs to the theme, which lives above this layer: Application
        /// fills it from Core::Theme::BorderColor so the window's edge matches the edges
        /// ImGui draws inside it. Left alone it is black, which is the honest answer for
        /// a caller that never said.
        Color borderColor;
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

        /// Withdraws a close the user asked for, so a layer can put a question on screen
        /// first. Harmless when no close is pending. The clear is permanent — nothing
        /// re-raises the flag on its own — so the caller owns asking again later via
        /// RequestClose().
        virtual void CancelClose() = 0;

        virtual void FramebufferSize(int& width, int& height) const = 0;
        virtual void SetVSync(bool enabled) = 0;

        /// Platform handle — for the ImGui backend. In practice a GLFWwindow*.
        [[nodiscard]] virtual void* GetNativeHandle() const = 0;

        virtual void Minimize() = 0;

        /// Maximizes a restored window and restores a maximized one — the behaviour the
        /// middle window button and a double-click on the caption both need.
        virtual void ToggleMaximize() = 0;
        [[nodiscard]] virtual bool IsMaximized() const = 0;

        /// Whether the system title bar is actually gone. False means the application
        /// must not paint its own, or the window ends up with two.
        [[nodiscard]] virtual bool HasCustomTitleBar() const = 0;

        /// Tells the window where the application-drawn caption is this frame. Ignored
        /// by a window that still has its system bar.
        virtual void SetCaptionMetrics(const CaptionMetrics& metrics) = 0;

        static Result<std::unique_ptr<Window>> Create(const WindowSpec& spec);
    };
}
