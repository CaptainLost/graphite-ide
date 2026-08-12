#include "corepch.h"

#include "Core/App/Application.h"

#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
#include "Core/Gui/Theme.h"

#include <GLFW/glfw3.h>

namespace Core {

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

	Layer& Application::PushLayer(Scope<Layer> layer)
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
		windowSpec.borderColor = Theme::GetBorderColor();

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
			// glfwGetTime is a double because it counts from process start; the step
			// between two frames is small enough that a float loses nothing, so the
			// subtraction happens in double and only the result narrows.
			const double now = glfwGetTime();
			const Timestep ts = static_cast<float>(now - m_LastFrameTime);
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

			// A minimized window has a zero-sized framebuffer. Neither updating nor
			// drawing into it accomplishes anything, and the GUI layer would be building
			// a frame for a surface with no pixels, so the whole body is skipped. Close
			// requests above are not: the window can still be closed from the taskbar.
			int width = 0;
			int height = 0;
			m_Window->FramebufferSize(width, height);
			m_Minimized = width == 0 || height == 0;

			if (!m_Minimized)
			{
				for (Layer* layer : m_FrameLayers)
					layer->OnUpdate(ts);

				m_Gui.BeginFrame();

				for (Layer* layer : m_FrameLayers)
					layer->OnUIRender();

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