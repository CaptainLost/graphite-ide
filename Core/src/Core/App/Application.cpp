#include "corepch.h"

#include "Core/App/Application.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
#include "Core/ImGui/Theme.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <utility>

namespace Core {

	Application* Application::s_Instance = nullptr;

	void Application::OnEvent(Event& event)
	{
		m_EventArrived = true;

		// Nothing here handles WindowClose; that runs through EveryLayerAllowsClose instead.
		// Events come from the window, which reports none before the GUI exists.
		if (m_Gui)
		{
			m_Gui->OnEvent(event);
		}

		for (auto layer = m_Layers.rbegin(); layer != m_Layers.rend(); ++layer)
		{
			if (event.Handled)
			{
				break;
			}

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
			{
				mayClose = false;
			}
		}

		return mayClose;
	}

	// A layer may push another layer during the frame, and a reallocated vector would
	// invalidate the iterator. This guards against insertion only; layer removal would need
	// rethinking.
	void Application::RefreshFrameLayers()
	{
		m_FrameLayers.clear();

		for (const Scope<Layer>& layer : m_Layers)
		{
			m_FrameLayers.push_back(layer.get());
		}
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
		windowSpec.Placement = m_Spec.Placement;
		// The window is created before the ImGui context exists, so the border colour is asked
		// of the theme here — the one layer that can see both — and travels down as a value.
		windowSpec.BorderColor = Theme::GetBorderColor();

		auto windowResult = Window::Create(windowSpec);
		if (!windowResult)
		{
			return windowResult.GetError();
		}

		m_Window = std::move(windowResult).GetValue();

		auto gui = ImGuiRenderer::Create(*m_Window, m_Spec.LayoutFile);
		if (!gui)
		{
			m_Window.reset();
			return gui.GetError();
		}

		m_Gui = std::move(gui).GetValue();

		m_Window->SetEventCallback([this](Event& event) { OnEvent(event); });

		return Result<void>::Ok();
	}

	// A minimized window draws nothing, so it never polls: with nothing to swap there is no
	// vsync to hold the loop back, and polling would spin a core. It still wakes for events
	// and for the idle tick, so a close from the taskbar is seen.
	void Application::WaitForReasonToDraw()
	{
		FramePacer::Clock::duration sleep =
			m_Minimized ? FramePacer::IdleTick
						: m_Pacer.SleepFor(FramePacer::Clock::now(), m_Gui->IsPointerHeld(),
										   Redraw::TakeDeadline());

		// A request already standing means its wake may be gone: anything on this thread that
		// pumps messages mid-frame — the shell's file operations do — can swallow the posted
		// one, and the wait would then sleep through a result it was told about.
		if (!m_Minimized && Redraw::IsRequested())
		{
			sleep = FramePacer::Clock::duration::zero();
		}

		if (sleep > FramePacer::Clock::duration::zero())
		{
			m_Window->WaitEvents(std::chrono::duration<double>(sleep).count());
		}
		else
		{
			m_Window->PollEvents();
		}

		// Each source is taken every time, not short-circuited: a request left untaken would
		// count as activity a second time on the next pass.
		const bool requested = Redraw::TakeRequest();
		const bool pendingInput = m_Gui->HasPendingInput();
		const bool evented = std::exchange(m_EventArrived, false);

		if (pendingInput || evented)
		{
			m_Pacer.NoteInput(FramePacer::Clock::now());
		}

		if (requested)
		{
			m_Pacer.NoteRequest();
		}
	}

	Result<void> Application::Run()
	{
		if (auto opened = OpenWindowAndGui(); !opened)
		{
			CORE_ERROR("Startup aborted: {}", opened.GetError().Message);
			return opened.GetError();
		}

		OnInit();

		// The loop's wait ends early on glfwPostEmptyEvent, which is safe from any thread
		// between glfwInit and glfwTerminate — the window's lifetime, so the waker is set for
		// exactly that long.
		Redraw::SetWaker(&glfwPostEmptyEvent);

		m_Running = true;
		m_LastFrameTime = glfwGetTime();

		// Startup counts as input: the first frames lay out the docked windows and load what
		// the layers restore, none of which arrives as an event.
		m_Pacer.NoteInput(FramePacer::Clock::now());

		while (m_Running)
		{
			WaitForReasonToDraw();

			// glfwGetTime counts in double from process start, so the subtraction happens
			// there and only the much smaller frame step narrows to float. After a sleep the
			// step covers all of it, so a timer advanced by it catches up in one frame.
			const double now = glfwGetTime();
			const Timestep ts{static_cast<float>(now - m_LastFrameTime)};
			m_LastFrameTime = now;

			RefreshFrameLayers();

			if (m_Window->ShouldClose())
			{
				if (EveryLayerAllowsClose())
				{
					break;
				}

				m_Window->CancelClose();
			}

			// A minimized window has a zero-sized framebuffer, so updating and drawing are
			// both skipped. Close requests above are not: it can still be closed from the
			// taskbar.
			m_Minimized = HasNoPixels();

			if (m_Minimized)
			{
				continue;
			}

			for (Layer* layer : m_FrameLayers)
			{
				layer->OnUpdate(ts);
			}

			m_Gui->BeginFrame();

			for (Layer* layer : m_FrameLayers)
			{
				layer->OnImGuiRender();
			}

			m_Gui->EndFrame(*m_Window);

			m_Window->SwapBuffers();
			m_Pacer.FrameDrawn();
		}

		Redraw::SetWaker(nullptr);

		OnShutdown();

		m_Layers.Clear();
		m_Gui.reset();
		m_Window.reset();

		return Result<void>::Ok();
	}
}