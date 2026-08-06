#include "Core/Base/Log.h"
#include "Core/Gui/ImGuiRenderer.h"
#include "Core/Platform/Window.h"
#include "Core/Version.h"

#include <imgui.h>

#include <memory>

int main()
{
    core::Logger::Get().AddSink(std::make_shared<core::ConsoleSink>());
    CORE_INFO("IDE {}", core::Version::String());

    auto windowResult = core::Window::Create(core::WindowSpec{});
    if (!windowResult)
    {
        CORE_ERROR("Start przerwany: {}", windowResult.GetError().message);
        return 1;
    }

    const std::unique_ptr<core::Window> window = std::move(windowResult).Value();

    core::ImGuiRenderer gui;
    if (const auto init = gui.Init(*window); !init)
    {
        CORE_ERROR("Start przerwany: {}", init.GetError().message);
        return 1;
    }

    while (!window->ShouldClose())
    {
        window->PollEvents();

        gui.BeginFrame();
        ImGui::ShowDemoWindow();
        gui.EndFrame(*window);

        window->SwapBuffers();
    }

    gui.Shutdown();
    CORE_INFO("IDE zamkniete");
    return 0;
}
