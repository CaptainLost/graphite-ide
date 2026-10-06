#include "corepch.h"

#include "Core/Platform/GlfwWindow.h"

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"
#include "Core/Platform/WindowPlacement.h"

#include <vector>

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

// <GL/gl.h> on Windows depends on typedefs from <windows.h>, so windows.h comes first.
#include <GL/gl.h>

#define GLFW_EXPOSE_NATIVE_WIN32

#include <GLFW/glfw3.h>

#include <GLFW/glfw3native.h>
#else
#include <GL/gl.h>

#include <GLFW/glfw3.h>
#endif

namespace Core {

	namespace {
		constexpr int kOpenGlMajorVersion = 3;
		constexpr int kOpenGlMinorVersion = 3;

		constexpr int kSwapOnEveryVerticalBlank = 1;
		constexpr int kSwapImmediately = 0;

		// GLFW truncates the timeout to whole milliseconds, so it wakes a hair before the deadline,
		// finds it not yet due and spends another frame asking again. A millisecond late is invisible.
		constexpr double kTimeoutTruncationMarginSeconds = 0.001;

		// glGetString answers only with a current context, and a driver may answer null, which the
		// formatter cannot take.
		const char* OpenGlStringOrUnknown(GLenum name)
		{
			const char* text = reinterpret_cast<const char*>(glGetString(name));
			return text != nullptr ? text : "unknown";
		}

		void LogGraphicsEnvironmentForBugReports()
		{
			CORE_INFO("GL vendor: {}", OpenGlStringOrUnknown(GL_VENDOR));
			CORE_INFO("GL renderer: {}", OpenGlStringOrUnknown(GL_RENDERER));
			CORE_INFO("GL version: {}", OpenGlStringOrUnknown(GL_VERSION));
			CORE_INFO("GLFW: {}", glfwGetVersionString());
		}

		int s_LiveWindowCount = 0;

		void OnGlfwError(int errorCode, const char* description)
		{
			CORE_ERROR("GLFW ({}): {}", errorCode, description);
		}
	}

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		ScreenRect ToScreenRect(const RECT& rectangle)
		{
			return ScreenRect{.Left = rectangle.left, .Top = rectangle.top, .Right = rectangle.right, .Bottom = rectangle.bottom};
		}

		BOOL CALLBACK CollectWorkArea(HMONITOR monitor, HDC, LPRECT, LPARAM workAreas)
		{
			MONITORINFO monitorInfo{};
			monitorInfo.cbSize = sizeof(monitorInfo);

			const bool hasMonitorInfo = GetMonitorInfoW(monitor, &monitorInfo) != 0;

			if (hasMonitorInfo)
			{
				reinterpret_cast<std::vector<ScreenRect>*>(workAreas)->push_back(ToScreenRect(monitorInfo.rcWork));
			}

			return TRUE;
		}

		std::vector<ScreenRect> MonitorWorkAreas()
		{
			std::vector<ScreenRect> workAreas;
			EnumDisplayMonitors(nullptr, nullptr, &CollectWorkArea, reinterpret_cast<LPARAM>(&workAreas));
			return workAreas;
		}

		MONITORINFO PrimaryMonitorInfo()
		{
			const POINT primaryMonitorOrigin{.x = 0, .y = 0};

			MONITORINFO monitorInfo{};
			monitorInfo.cbSize = sizeof(monitorInfo);
			GetMonitorInfoW(MonitorFromPoint(primaryMonitorOrigin, MONITOR_DEFAULTTOPRIMARY), &monitorInfo);
			return monitorInfo;
		}
	}
#endif

	GlfwWindow::GlfwWindow(GLFWwindow* handle) : m_Handle(handle) {}

	namespace {
		KeyModifier ToKeyModifier(int glfwModifierBits)
		{
			KeyModifier modifiers = KeyModifier::None;

			if ((glfwModifierBits & GLFW_MOD_SHIFT) != 0)
			{
				modifiers = modifiers | KeyModifier::Shift;
			}
			if ((glfwModifierBits & GLFW_MOD_CONTROL) != 0)
			{
				modifiers = modifiers | KeyModifier::Control;
			}
			if ((glfwModifierBits & GLFW_MOD_ALT) != 0)
			{
				modifiers = modifiers | KeyModifier::Alt;
			}
			if ((glfwModifierBits & GLFW_MOD_SUPER) != 0)
			{
				modifiers = modifiers | KeyModifier::Super;
			}

			return modifiers;
		}
	}

	void GlfwWindow::InstallCallbacks()
	{
		glfwSetWindowUserPointer(m_Handle, this);

		glfwSetWindowSizeCallback(m_Handle, &OnResized);
		glfwSetWindowCloseCallback(m_Handle, &OnCloseRequested);
		glfwSetWindowFocusCallback(m_Handle, &OnFocusChanged);
		glfwSetWindowPosCallback(m_Handle, &OnMoved);
		glfwSetKeyCallback(m_Handle, &OnKey);
		glfwSetCharCallback(m_Handle, &OnCharacterTyped);
		glfwSetMouseButtonCallback(m_Handle, &OnMouseButton);
		glfwSetScrollCallback(m_Handle, &OnScrolled);
		glfwSetCursorPosCallback(m_Handle, &OnCursorMoved);
		glfwSetDropCallback(m_Handle, &OnFilesDropped);
	}

	GlfwWindow* GlfwWindow::ListeningWindowOf(GLFWwindow* handle)
	{
		GlfwWindow* window = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(handle));
		const bool nobodyIsListening = window == nullptr || !window->m_EventCallback;

		if (nobodyIsListening)
		{
			return nullptr;
		}

		return window;
	}

	void GlfwWindow::OnResized(GLFWwindow* handle, int width, int height)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		WindowResizeEvent event{width, height};
		window->m_EventCallback(event);
	}

	void GlfwWindow::OnCloseRequested(GLFWwindow* handle)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		WindowCloseEvent event;
		window->m_EventCallback(event);
	}

	void GlfwWindow::OnFocusChanged(GLFWwindow* handle, int focused)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		const bool gainedFocus = focused == GLFW_TRUE;

		if (gainedFocus)
		{
			WindowFocusEvent event;
			window->m_EventCallback(event);
		}
		else
		{
			WindowLostFocusEvent event;
			window->m_EventCallback(event);
		}
	}

	void GlfwWindow::OnMoved(GLFWwindow* handle, int x, int y)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		WindowMovedEvent event{x, y};
		window->m_EventCallback(event);
	}

	void GlfwWindow::OnKey(GLFWwindow* handle, int key, int, int action, int modifierBits)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		const KeyCode keyCode = static_cast<KeyCode>(key);
		const KeyModifier modifiers = ToKeyModifier(modifierBits);

		switch (action)
		{
		case GLFW_PRESS:
		{
			const bool isRepeat = false;
			KeyPressedEvent event{keyCode, modifiers, isRepeat};
			window->m_EventCallback(event);
			break;
		}
		case GLFW_REPEAT:
		{
			const bool isRepeat = true;
			KeyPressedEvent event{keyCode, modifiers, isRepeat};
			window->m_EventCallback(event);
			break;
		}
		case GLFW_RELEASE:
		{
			KeyReleasedEvent event{keyCode, modifiers};
			window->m_EventCallback(event);
			break;
		}
		default:
			break;
		}
	}

	void GlfwWindow::OnCharacterTyped(GLFWwindow* handle, unsigned int codepoint)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		KeyTypedEvent event{codepoint};
		window->m_EventCallback(event);
	}

	void GlfwWindow::OnMouseButton(GLFWwindow* handle, int button, int action, int)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		const MouseCode mouseButton = static_cast<MouseCode>(button);

		if (action == GLFW_PRESS)
		{
			MouseButtonPressedEvent event{mouseButton};
			window->m_EventCallback(event);
		}
		else if (action == GLFW_RELEASE)
		{
			MouseButtonReleasedEvent event{mouseButton};
			window->m_EventCallback(event);
		}
	}

	void GlfwWindow::OnScrolled(GLFWwindow* handle, double xOffset, double yOffset)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		MouseScrolledEvent event{static_cast<float>(xOffset), static_cast<float>(yOffset)};
		window->m_EventCallback(event);
	}

	void GlfwWindow::OnCursorMoved(GLFWwindow* handle, double x, double y)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		MouseMovedEvent event{static_cast<float>(x), static_cast<float>(y)};
		window->m_EventCallback(event);
	}

	// GLFW hands dropped paths over as UTF-8 on every platform, and std::filesystem::path's own
	// constructor would read them in the ANSI codepage and mangle every path outside it. The ImGui
	// GLFW backend installs no drop callback, so it neither chains to nor replaces this one.
	void GlfwWindow::OnFilesDropped(GLFWwindow* handle, int pathCount, const char** paths)
	{
		GlfwWindow* window = ListeningWindowOf(handle);
		if (window == nullptr)
		{
			return;
		}

		std::vector<std::filesystem::path> droppedPaths;
		droppedPaths.reserve(static_cast<std::size_t>(pathCount));

		for (int pathIndex = 0; pathIndex < pathCount; ++pathIndex)
		{
			droppedPaths.push_back(FromUtf8(paths[pathIndex]));
		}

		FileDropEvent event{std::move(droppedPaths)};
		window->m_EventCallback(event);
	}

	GlfwWindow::~GlfwWindow()
	{
		// The frame restores a window procedure, so it goes before glfwDestroyWindow: a destroyed
		// window has none left to restore it to.
		m_Frame.reset();

		if (m_Handle == nullptr)
		{
			return;
		}

		glfwDestroyWindow(m_Handle);
		m_Handle = nullptr;

		--s_LiveWindowCount;
		const bool wasLastWindow = s_LiveWindowCount == 0;

		if (wasLastWindow)
		{
			glfwTerminate();
		}
	}

	void GlfwWindow::PollEvents()
	{
		glfwPollEvents();
	}

	void GlfwWindow::WaitEvents(double timeoutSeconds)
	{
		glfwWaitEventsTimeout(timeoutSeconds + kTimeoutTruncationMarginSeconds);
	}

	void GlfwWindow::SwapBuffers()
	{
		glfwSwapBuffers(m_Handle);
	}

	bool GlfwWindow::ShouldClose() const
	{
		return glfwWindowShouldClose(m_Handle) != 0;
	}

	void GlfwWindow::RequestClose()
	{
		glfwSetWindowShouldClose(m_Handle, GLFW_TRUE);
	}

	void GlfwWindow::CancelClose()
	{
		glfwSetWindowShouldClose(m_Handle, GLFW_FALSE);
	}

	void GlfwWindow::FramebufferSize(int& width, int& height) const
	{
		glfwGetFramebufferSize(m_Handle, &width, &height);
	}

	void GlfwWindow::SetVSync(bool enabled)
	{
		glfwMakeContextCurrent(m_Handle);

		const int swapInterval = enabled ? kSwapOnEveryVerticalBlank : kSwapImmediately;
		glfwSwapInterval(swapInterval);
	}

	void GlfwWindow::Minimize()
	{
		glfwIconifyWindow(m_Handle);
	}

	void GlfwWindow::ToggleMaximize()
	{
		if (IsMaximized())
		{
			glfwRestoreWindow(m_Handle);
			return;
		}

		glfwMaximizeWindow(m_Handle);
	}

	bool GlfwWindow::IsMaximized() const
	{
		return glfwGetWindowAttrib(m_Handle, GLFW_MAXIMIZED) == GLFW_TRUE;
	}

	bool GlfwWindow::IsFocused() const
	{
		return glfwGetWindowAttrib(m_Handle, GLFW_FOCUSED) == GLFW_TRUE;
	}

	WindowPlacement GlfwWindow::GetPlacement() const
	{
#ifdef CORE_PLATFORM_WINDOWS
		WINDOWPLACEMENT nativePlacement{};
		nativePlacement.length = sizeof(nativePlacement);

		const bool placementRead = GetWindowPlacement(glfwGetWin32Window(m_Handle), &nativePlacement) != 0;

		if (placementRead)
		{
			const RECT& restoredRectangle = nativePlacement.rcNormalPosition;

			const bool shownMaximized = nativePlacement.showCmd == SW_SHOWMAXIMIZED;
			const bool restoresToMaximized = (nativePlacement.flags & WPF_RESTORETOMAXIMIZED) != 0;
			const bool minimizedFromMaximized = nativePlacement.showCmd == SW_SHOWMINIMIZED && restoresToMaximized;
			const bool reopensMaximized = shownMaximized || minimizedFromMaximized;

			return WindowPlacement{.X = restoredRectangle.left,
								   .Y = restoredRectangle.top,
								   .Width = restoredRectangle.right - restoredRectangle.left,
								   .Height = restoredRectangle.bottom - restoredRectangle.top,
								   .Maximized = reopensMaximized};
		}
#endif
		WindowPlacement placement;
		glfwGetWindowPos(m_Handle, &placement.X, &placement.Y);
		glfwGetWindowSize(m_Handle, &placement.Width, &placement.Height);
		placement.Maximized = IsMaximized();
		return placement;
	}

	void GlfwWindow::Show(const std::optional<WindowPlacement>& placement)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// rcNormalPosition is in workspace coordinates: screen coordinates shifted by a taskbar docked
		// on the primary monitor's top or left edge. The monitors report screen coordinates, so the
		// placement is fitted there and shifted back.
		const MONITORINFO primaryMonitorInfo = PrimaryMonitorInfo();
		const int workspaceShiftX = primaryMonitorInfo.rcWork.left - primaryMonitorInfo.rcMonitor.left;
		const int workspaceShiftY = primaryMonitorInfo.rcWork.top - primaryMonitorInfo.rcMonitor.top;

		WindowPlacement fitted;

		if (placement)
		{
			WindowPlacement onScreen = *placement;
			onScreen.X += workspaceShiftX;
			onScreen.Y += workspaceShiftY;

			const std::vector<ScreenRect> workAreas = MonitorWorkAreas();
			fitted = FitToMonitors(onScreen, workAreas, ToScreenRect(primaryMonitorInfo.rcWork));
		}
		else
		{
			int createdWidth = 0;
			int createdHeight = 0;
			glfwGetWindowSize(m_Handle, &createdWidth, &createdHeight);
			fitted = DefaultPlacement(createdWidth, createdHeight, ToScreenRect(primaryMonitorInfo.rcWork));
		}

		const int workspaceLeft = fitted.X - workspaceShiftX;
		const int workspaceTop = fitted.Y - workspaceShiftY;

		WINDOWPLACEMENT nativePlacement{};
		nativePlacement.length = sizeof(nativePlacement);
		nativePlacement.showCmd = fitted.Maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
		nativePlacement.rcNormalPosition = RECT{.left = workspaceLeft,
												.top = workspaceTop,
												.right = workspaceLeft + fitted.Width,
												.bottom = workspaceTop + fitted.Height};

		const HWND nativeWindow = glfwGetWin32Window(m_Handle);
		const bool placedAndShown = SetWindowPlacement(nativeWindow, &nativePlacement) != 0;

		if (placedAndShown)
		{
			// Placed on a monitor with another DPI, the window receives WM_DPICHANGED and resizes to the
			// restored rectangle scaled by the ratio of the two DPIs, so a saved size would grow on every
			// launch. It is at the target monitor's DPI by now, so placing it again is not rescaled.
			SetWindowPlacement(nativeWindow, &nativePlacement);
			return;
		}

		CORE_WARN("Cannot restore the window's placement (error {}), opening it at the default",
				  GetLastError());
		glfwShowWindow(m_Handle);
#else
		if (!placement)
		{
			int workAreaX = 0;
			int workAreaY = 0;
			int workAreaWidth = 0;
			int workAreaHeight = 0;
			int createdWidth = 0;
			int createdHeight = 0;

			GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();

			if (primaryMonitor != nullptr)
			{
				glfwGetMonitorWorkarea(primaryMonitor, &workAreaX, &workAreaY, &workAreaWidth, &workAreaHeight);
				glfwGetWindowSize(m_Handle, &createdWidth, &createdHeight);

				const ScreenRect workArea{.Left = workAreaX,
										  .Top = workAreaY,
										  .Right = workAreaX + workAreaWidth,
										  .Bottom = workAreaY + workAreaHeight};
				const WindowPlacement centred = DefaultPlacement(createdWidth, createdHeight, workArea);
				glfwSetWindowPos(m_Handle, centred.X, centred.Y);
				glfwSetWindowSize(m_Handle, centred.Width, centred.Height);
			}

			glfwShowWindow(m_Handle);
			return;
		}

		glfwSetWindowPos(m_Handle, placement->X, placement->Y);
		glfwSetWindowSize(m_Handle, placement->Width, placement->Height);
		glfwShowWindow(m_Handle);

		if (placement->Maximized)
		{
			glfwMaximizeWindow(m_Handle);
		}
#endif
	}

	bool GlfwWindow::HasCustomTitleBar() const
	{
		return m_Frame != nullptr;
	}

	void GlfwWindow::SetCaptionMetrics(const CaptionMetrics& metrics)
	{
		if (m_Frame != nullptr)
		{
			m_Frame->SetCaptionMetrics(metrics);
		}
	}

	void GlfwWindow::SetBorderColor(const Color& color)
	{
		const bool systemOwnsTheBorder = m_Frame == nullptr;

		if (systemOwnsTheBorder)
		{
			return;
		}

		m_Frame->SetBorderColor(color);
	}

	Result<Scope<Window>> Window::Create(const WindowSpec& spec)
	{
		const bool isFirstWindow = s_LiveWindowCount == 0;

		if (isFirstWindow)
		{
			glfwSetErrorCallback(&OnGlfwError);

			const bool initialised = glfwInit() != GLFW_FALSE;

			if (!initialised)
			{
				return Error{.Code = ErrorCode::Unsupported, .Message = "glfwInit failed"};
			}
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kOpenGlMajorVersion);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kOpenGlMinorVersion);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

		// Created hidden and shown once Show has placed it, so a restored window does not flash up
		// at the default size first.
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		GLFWmonitor* const noFullScreenMonitor = nullptr;
		GLFWwindow* const noSharedContext = nullptr;
		GLFWwindow* handle =
			glfwCreateWindow(spec.Width, spec.Height, spec.Title.c_str(), noFullScreenMonitor, noSharedContext);

		// GLFW keeps hints after the call, and ImGui's platform windows are GLFW windows too.
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

		if (handle == nullptr)
		{
			if (isFirstWindow)
			{
				glfwTerminate();
			}

			return Error{.Code = ErrorCode::Unsupported, .Message = "failed to create an OpenGL 3.3 window"};
		}

		++s_LiveWindowCount;

		auto window = CreateScope<GlfwWindow>(handle);
		window->InstallCallbacks();
		glfwMakeContextCurrent(handle);
		window->SetVSync(spec.VSync);
		LogGraphicsEnvironmentForBugReports();

		if (spec.CustomTitleBar)
		{
			Result<Scope<WindowFrame>> frame = WindowFrame::Hide(*window, spec.BorderColor);
			if (frame)
			{
				window->AdoptFrame(std::move(frame).GetValue());
			}
			else
			{
				CORE_WARN("Keeping the system title bar: {}", frame.GetError().Message);
			}
		}

		window->Show(spec.Placement);

		CORE_INFO("Window created: {}x{} \"{}\"", spec.Width, spec.Height, spec.Title);
		return Result<Scope<Window>>{std::move(window)};
	}
}
