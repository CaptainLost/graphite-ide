#include "Core/Base/Log.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Platform/Window.h"
#include "Core/Version.h"

#include <windows.h>

#include <GL/gl.h>

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

    while (!window->ShouldClose())
    {
        window->PollEvents();

        int width = 0;
        int height = 0;
        window->FramebufferSize(width, height);

        glViewport(0, 0, width, height);
        glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        window->SwapBuffers();
    }

    CORE_INFO("IDE zamkniete");
    return 0;
}
