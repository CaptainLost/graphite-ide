#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

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

    private:
        bool m_initialized = false;
    };
}
