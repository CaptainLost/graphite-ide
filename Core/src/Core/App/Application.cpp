#include "corepch.h"

#include "Core/App/Application.h"

#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
#include "Core/Gui/Theme.h"

#include <GLFW/glfw3.h>

namespace Core
{
    Application* Application::s_Instance = nullptr;

    Application::Application(ApplicationSpec spec) : m_Spec(std::move(spec))
    {
        CORE_ASSERT(s_Instance == nullptr, "Application already exists");
        s_Instance = this;
    }

    Application::~Application()
    {
        s_Instance = nullptr;
    }

    Application& Application::Get()
    {
        CORE_ASSERT(s_Instance != nullptr, "Application::Get called before the application was created");
        return *s_Instance;
    }

    Layer& Application::PushLayer(std::unique_ptr<Layer> layer)
    {
        return m_Layers.Push(std::move(layer));
    }

    void Application::Close()
    {
        m_Running = false;
    }

    Result<void> Application::Run()
    {
        WindowSpec windowSpec;
        windowSpec.title = m_Spec.name;
        windowSpec.width = m_Spec.width;
        windowSpec.height = m_Spec.height;
        windowSpec.customTitleBar = m_Spec.customTitleBar;
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

        m_Window = std::move(windowResult).GetValue();

        if (auto init = m_Gui.Init(*m_Window); !init)
        {
            CORE_ERROR("Startup aborted: {}", init.GetError().message);
            m_Window.reset();
            return init.GetError();
        }

        OnInit();

        m_Running = true;
        m_LastFrameTime = glfwGetTime();

        while (m_Running)
        {
            const double now = glfwGetTime();
            const auto delta = static_cast<float>(now - m_LastFrameTime);
            m_LastFrameTime = now;

            m_Window->PollEvents();

            // We iterate over a copy of the pointers: a layer may push another layer during
            // the frame, and a reallocated vector would invalidate the iterator. The copy guards
            // against insertion — if layer removal is ever introduced, this will need rethinking.
            m_FrameLayers.clear();
            for (auto& layer : m_Layers)
                m_FrameLayers.push_back(layer.get());

            if (m_Window->ShouldClose())
            {
                // Every layer is asked, not just up to the first refusal: a layer that
                // refuses usually does so by opening a question, and one that never gets
                // asked never opens its own.
                bool mayClose = true;
                for (Layer* layer : m_FrameLayers)
                {
                    if (!layer->OnCloseRequested())
                        mayClose = false;
                }

                if (mayClose)
                    break;

                m_Window->CancelClose();
            }

            for (Layer* layer : m_FrameLayers)
                layer->OnUpdate(delta);

            m_Gui.BeginFrame();

            for (Layer* layer : m_FrameLayers)
                layer->OnUIRender();

            m_Gui.EndFrame(*m_Window);
            m_Window->SwapBuffers();
        }

        OnShutdown();

        m_Layers.Clear();
        m_Gui.Shutdown();
        m_Window.reset();

        return Result<void>::Ok();
    }
}
