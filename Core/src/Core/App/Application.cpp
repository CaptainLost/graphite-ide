#include "corepch.h"

#include "Core/App/Application.h"

#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
#include "Core/ImGui/Theme.h"

#include <GLFW/glfw3.h>

namespace Core {

	Application* Application::s_Instance = nullptr;

	void Application::OnEvent(Event& event)
	{
		// Nothing here handles WindowClose; that runs through EveryLayerAllowsClose instead.
		m_Gui.OnEvent(event);

		for (auto layer = m_Layers.rbegin(); layer != m_Layers.rend(); ++layer)
		{
			if (event.Handled)
				break;

			(*layer)->OnEvent(event);
		}
	}

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

	Layer& Application::PushLayer(Scope<Layer> layer)
	{
		return m_Layers.Push(std::move(layer));
	}

	void Application::Close()
	{
		m_Running = false;
	}

	// A layer that refuses usually does so by opening a question, and one that is never asked
	// never opens its own — so every layer is asked, not just up to the first refusal.
	bool Application::EveryLayerAllowsClose()
	{
		bool mayClose = true;

		for (Layer* layer : m_FrameLayers)
		{
			if (!layer->OnCloseRequested())
				mayClose = false;
		}

		return mayClose;
	}

	// A layer may push another layer during the frame, and a reallocated vector would
	// invalidate the iterator. This guards against insertion only; layer removal would need
	// rethinking.
	void Application::RefreshFrameLayers()
	{
		m_FrameLayers.clear();

		for (auto& layer : m_Layers)
			m_FrameLayers.push_back(layer.get());
	}

	bool Application::HasNoPixels() const
	{
		int width = 0;
		int height = 0;
		m_Window->FramebufferSize(width, height);

		return width == 0 || height == 0;
	}

	Result<void> Application::OpenWindowAndGui()
	{
		WindowSpec windowSpec;
		windowSpec.Title = m_Spec.Name;
		windowSpec.Width = m_Spec.Width;
		windowSpec.Height = m_Spec.Height;
		windowSpec.CustomTitleBar = m_Spec.CustomTitleBar;
		// The window is created before the ImGui context exists, so the border colour is asked
		// of the theme here — the one layer that can see both — and travels down as a value.
		windowSpec.BorderColor = Theme::GetBorderColor();

		auto windowResult = Window::Create(windowSpec);
		if (!windowResult)
		{
			return windowResult.GetError();
		}

		m_Window = std::move(windowResult).GetValue();

		if (auto init = m_Gui.Init(*m_Window); !init)
		{
			m_Window.reset();
			return init.GetError();
		}

		m_Window->SetEventCallback(CORE_BIND_EVENT_FN(Application::OnEvent));

		return Result<void>::Ok();
	}

	Result<void> Application::Run()
	{
		if (auto opened = OpenWindowAndGui(); !opened)
		{
			CORE_ERROR("Startup aborted: {}", opened.GetError().Message);
			return opened.GetError();
		}

		OnInit();

		m_Running = true;
		m_LastFrameTime = glfwGetTime();

		while (m_Running)
		{
			// glfwGetTime counts in double from process start, so the subtraction happens
			// there and only the much smaller frame step narrows to float.
			const double now = glfwGetTime();
			const Timestep ts = static_cast<float>(now - m_LastFrameTime);
			m_LastFrameTime = now;

			m_Window->PollEvents();
			RefreshFrameLayers();

			if (m_Window->ShouldClose())
			{
				if (EveryLayerAllowsClose())
					break;

				m_Window->CancelClose();
			}

			// A minimized window has a zero-sized framebuffer, so updating and drawing are
			// both skipped. Close requests above are not: it can still be closed from the
			// taskbar.
			m_Minimized = HasNoPixels();

			if (!m_Minimized)
			{
				for (Layer* layer : m_FrameLayers)
					layer->OnUpdate(ts);

				m_Gui.BeginFrame();

				for (Layer* layer : m_FrameLayers)
					layer->OnImGuiRender();

				m_Gui.EndFrame(*m_Window);
			}

			m_Window->SwapBuffers();
		}

		OnShutdown();

		m_Layers.Clear();
		m_Gui.Shutdown();
		m_Window.reset();

		return Result<void>::Ok();
	}
}