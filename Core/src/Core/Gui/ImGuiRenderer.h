#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

namespace core
{
    class Window;

    /// Cykl zycia Dear ImGui razem z backendami GLFW i OpenGL3.
    /// Wlacza docking oraz viewporty i utrzymuje uklad w pliku imgui.ini.
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
