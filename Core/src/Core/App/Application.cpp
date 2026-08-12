#include "corepch.h"

#include "Core/App/Application.h"

#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
#include "Core/Gui/Theme.h"

#include <GLFW/glfw3.h>

namespace Core
{
    Application* Application::s_instance = nullptr;

    Application::Application(ApplicationSpec spec) : m_spec(std::move(spec))
    {
        CORE_ASSERT(s_instance == nullptr, "Application already exists");
        s_instance = this;
    }

    Application::~Application()
    {
        s_instance = nullptr;
    }

    Application& Application::Get()
    {
        CORE_ASSERT(s_instance != nullptr, "Application::Get called before the application was created");
        return *s_instance;
    }

    Layer& Application::PushLayer(std::unique_ptr<Layer> layer)
    {
        return m_layers.Push(std::move(layer));
    }

    void Application::Close()
    {
        m_running = false;
    }

    Result<void> Application::Run()
    {
        WindowSpec windowSpec;
        windowSpec.title = m_spec.name;
        windowSpec.width = m_spec.width;
        windowSpec.height = m_spec.height;
        windowSpec.customTitleBar = m_spec.customTitleBar;
        // The window is created before the ImGui context exists, so the border colour is
        // asked of the theme here — the one layer that can see both — and travels down as
        // a value. Core::Platform never learns that there is a theme.
        windowSpec.borderColor = Theme::BorderColor();

        auto windowResult = Window::Create(windowSpec);
        if (!windowResult)
        {
            CORE_ERROR("Startup aborted: {}", windowResult.GetError().message);
            return windowResult.GetError();
        }

        m_window = std::move(windowResult).Value();

        if (auto init = m_gui.Init(*m_window); !init)
        {
            CORE_ERROR("Startup aborted: {}", init.GetError().message);
            m_window.reset();
            return init.GetError();
        }

        OnInit();

        m_running = true;
        m_lastFrameTime = glfwGetTime();

        while (m_running)
        {
            const double now = glfwGetTime();
            const auto delta = static_cast<float>(now - m_lastFrameTime);
            m_lastFrameTime = now;

            m_window->PollEvents();

            // We iterate over a copy of the pointers: a layer may push another layer during
            // the frame, and a reallocated vector would invalidate the iterator. The copy guards
            // against insertion — if layer removal is ever introduced, this will need rethinking.
            m_frameLayers.clear();
            for (auto& layer : m_layers)
                m_frameLayers.push_back(layer.get());

            if (m_window->ShouldClose())
            {
                // Every layer is asked, not just up to the first refusal: a layer that
                // refuses usually does so by opening a question, and one that never gets
                // asked never opens its own.
                bool mayClose = true;
                for (Layer* layer : m_frameLayers)
                {
                    if (!layer->OnCloseRequested())
                        mayClose = false;
                }

                if (mayClose)
                    break;

                m_window->CancelClose();
            }

            for (Layer* layer : m_frameLayers)
                layer->OnUpdate(delta);

            m_gui.BeginFrame();

            for (Layer* layer : m_frameLayers)
                layer->OnUIRender();

            m_gui.EndFrame(*m_window);
            m_window->SwapBuffers();
        }

        OnShutdown();

        m_layers.Clear();
        m_gui.Shutdown();
        m_window.reset();

        return Result<void>::Ok();
    }
}
