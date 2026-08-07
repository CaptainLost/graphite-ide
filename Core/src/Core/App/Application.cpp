#include "corepch.h"

#include "Core/App/Application.h"

#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"

#include <GLFW/glfw3.h>

namespace core
{
    Application* Application::s_instance = nullptr;

    Application::Application(ApplicationSpec spec) : m_spec(std::move(spec))
    {
        CORE_ASSERT(s_instance == nullptr, "Application juz istnieje");
        s_instance = this;
    }

    Application::~Application()
    {
        s_instance = nullptr;
    }

    Application& Application::Get()
    {
        CORE_ASSERT(s_instance != nullptr, "Application::Get przed utworzeniem aplikacji");
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

    void Application::Run()
    {
        WindowSpec windowSpec;
        windowSpec.title = m_spec.name;
        windowSpec.width = m_spec.width;
        windowSpec.height = m_spec.height;

        auto windowResult = Window::Create(windowSpec);
        if (!windowResult)
        {
            CORE_ERROR("Start przerwany: {}", windowResult.GetError().message);
            return;
        }

        m_window = std::move(windowResult).Value();

        if (const auto init = m_gui.Init(*m_window); !init)
        {
            CORE_ERROR("Start przerwany: {}", init.GetError().message);
            m_window.reset();
            return;
        }

        OnInit();

        m_running = true;
        m_lastFrameTime = glfwGetTime();

        while (m_running && !m_window->ShouldClose())
        {
            const double now = glfwGetTime();
            const auto delta = static_cast<float>(now - m_lastFrameTime);
            m_lastFrameTime = now;

            m_window->PollEvents();

            // Iterujemy po kopii wskaznikow: warstwa moze w trakcie klatki dodac kolejna warstwe,
            // a przealokowany vector uniewaznilby iterator. Kopia chroni przed dodaniem — gdyby
            // kiedys pojawilo sie usuwanie warstw, trzeba ja bedzie przemyslec ponownie.
            m_frameLayers.clear();
            for (auto& layer : m_layers)
                m_frameLayers.push_back(layer.get());

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
    }
}
