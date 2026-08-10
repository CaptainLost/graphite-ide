#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

struct ImFont;

namespace core
{
    class Window;

    /// Lifecycle of Dear ImGui together with the GLFW and OpenGL3 backends.
    /// Enables docking and viewports and keeps the layout in the imgui.ini file.
    class ImGuiRenderer final : NonCopyable
    {
    public:
        ImGuiRenderer() = default;
        ~ImGuiRenderer();

        Result<void> Init(Window& window);
        void Shutdown();

        void BeginFrame();
        void EndFrame(Window& window);

        /// The editor and UI font. Null until Init succeeds, and if the file is missing —
        /// callers pass null to ImGui::PushFont, which keeps the default font rather than
        /// crashing, so a missing asset degrades instead of failing.
        [[nodiscard]] static ImFont* MonoFont() noexcept;

    private:
        bool m_initialized = false;
    };
}
