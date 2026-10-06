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

	// Nothing here handles WindowClose: closing goes through EveryLayerAllowsClose instead.
	void Application::OnEvent(Event& event)
	{
		m_EventArrived = true;

		if (m_Gui != nullptr)
		{
			m_Gui->OnEvent(event);
		}

		m_Layers.OfferEventFromTheTopUntilHandled(event);
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

	// Every layer is asked, not just those up to the first refusal: a layer that refuses usually
	// does so by opening a question, and one that is never asked never opens its own.
	bool Application::EveryLayerAllowsClose()
	{
		bool mayClose = true;

		for (Layer* layer : m_LayersThisFrame)
		{
			if (!layer->OnCloseRequested())
			{
				mayClose = false;
			}
		}

		return mayClose;
	}

	// A copy, because a layer may push another layer during the frame, and a reallocated vector
	// would invalidate a loop over the stack itself. This guards against insertion only; removing
	// a layer mid-frame would need rethinking.
	void Application::SnapshotLayersForThisFrame()
	{
		m_LayersThisFrame.clear();

		for (const Scope<Layer>& layer : m_Layers)
		{
			m_LayersThisFrame.push_back(layer.get());
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
		const WindowSpec windowSpec{
			.Title = m_Spec.Name,
			.Width = m_Spec.Width,
			.Height = m_Spec.Height,
			.CustomTitleBar = m_Spec.CustomTitleBar,
			// The window is created before the ImGui context exists, so the border colour is asked
			// of the theme here, the one place that can see both, and travels down as a value.
			.BorderColor = Theme::GetBorderColor(),
			.Placement = m_Spec.Placement,
		};

		Result<Scope<Window>> windowResult = Window::Create(windowSpec);
		if (!windowResult)
		{
			return windowResult.GetError();
		}

		m_Window = std::move(windowResult).GetValue();

		Result<Scope<ImGuiRenderer>> guiResult = ImGuiRenderer::Create(*m_Window, m_Spec.LayoutFile);
		if (!guiResult)
		{
			m_Window.reset();
			return guiResult.GetError();
		}

		m_Gui = std::move(guiResult).GetValue();

		m_Window->SetEventCallback([this](Event& event) { OnEvent(event); });

		return Result<void>::Ok();
	}

	void Application::WaitForReasonToDraw()
	{
		// A minimized window must not poll: with nothing to swap there is no vsync to hold the
		// loop back, and polling would spin a core. It waits for events and the idle tick
		// instead, so a close from the taskbar is still seen.
		FramePacer::Clock::duration sleep = FramePacer::kIdleTick;

		if (!m_Minimized)
		{
			const std::optional<FramePacer::Clock::time_point> deadline = Redraw::TakeDeadline();
			const bool pointerHeld = m_Gui->IsPointerHeld();
			sleep = m_Pacer.SleepFor(FramePacer::Clock::now(), pointerHeld, deadline);
		}

		// A request already standing means its wake may be gone: anything on this thread that
		// pumps messages mid-frame, as the shell's file operations do, can swallow the posted
		// one, and the wait would then sleep through a result it was told about.
		const bool requestAlreadyStanding = !m_Minimized && Redraw::IsRequested();

		if (requestAlreadyStanding)
		{
			sleep = FramePacer::Clock::duration::zero();
		}

		const bool hasTimeToSleep = sleep > FramePacer::Clock::duration::zero();

		if (hasTimeToSleep)
		{
			const double sleepSeconds = std::chrono::duration<double>(sleep).count();
			m_Window->WaitEvents(sleepSeconds);
		}
		else
		{
			m_Window->PollEvents();
		}

		// Each source is taken every time, not short-circuited: a request left untaken would
		// count as activity a second time on the next pass.
		const bool redrawRequested = Redraw::TakeRequest();
		const bool inputPending = m_Gui->HasPendingInput();
		const bool eventArrived = std::exchange(m_EventArrived, false);

		if (inputPending || eventArrived)
		{
			m_Pacer.NoteInput(FramePacer::Clock::now());
		}

		if (redrawRequested)
		{
			m_Pacer.NoteRequest();
		}
	}

	Result<void> Application::Run()
	{
		if (const Result<void> openResult = OpenWindowAndGui(); !openResult)
		{
			CORE_ERROR("Startup aborted: {}", openResult.GetError().Message);
			return openResult.GetError();
		}

		OnInit();

		// glfwPostEmptyEvent is safe from any thread only between glfwInit and glfwTerminate,
		// which is the window's lifetime, so the waker is set for exactly that long.
		Redraw::SetWaker(&glfwPostEmptyEvent);

		m_Running = true;
		m_LastFrameStartSeconds = glfwGetTime();

		// Startup counts as input: the first frames lay out the docked windows and load what
		// the layers restore, none of which arrives as an event.
		m_Pacer.NoteInput(FramePacer::Clock::now());

		while (m_Running)
		{
			WaitForReasonToDraw();

			// glfwGetTime counts in double from process start, so the subtraction happens in
			// double and only the much smaller frame step narrows to float.
			const double frameStartSeconds = glfwGetTime();
			const Timestep timeSinceLastFrame{static_cast<float>(frameStartSeconds - m_LastFrameStartSeconds)};
			m_LastFrameStartSeconds = frameStartSeconds;

			SnapshotLayersForThisFrame();

			if (m_Window->ShouldClose())
			{
				if (EveryLayerAllowsClose())
				{
					break;
				}

				m_Window->CancelClose();
			}

			// A minimized window has a zero-sized framebuffer. This comes after the close
			// request on purpose, so a minimized window can still be closed from the taskbar.
			m_Minimized = HasNoPixels();

			if (m_Minimized)
			{
				continue;
			}

			for (Layer* layer : m_LayersThisFrame)
			{
				layer->OnUpdate(timeSinceLastFrame);
			}

			m_Gui->BeginFrame();

			for (Layer* layer : m_LayersThisFrame)
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