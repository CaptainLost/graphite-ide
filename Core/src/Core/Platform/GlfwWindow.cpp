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
		// Needs a current context. A driver that answers nothing is reported as such rather than
		// handed to the formatter as a null string.
		const char* GlString(GLenum name)
		{
			const auto* text = reinterpret_cast<const char*>(glGetString(name));
			return text != nullptr ? text : "unknown";
		}

		// A bug report that says what drew the window is worth one line per fact in the file.
		void LogGraphicsEnvironment()
		{
			CORE_INFO("GL vendor: {}", GlString(GL_VENDOR));
			CORE_INFO("GL renderer: {}", GlString(GL_RENDERER));
			CORE_INFO("GL version: {}", GlString(GL_VERSION));
			CORE_INFO("GLFW: {}", glfwGetVersionString());
		}

		int s_GlfwWindowCount = 0;

		void OnGlfwError(int code, const char* description)
		{
			CORE_ERROR("GLFW ({}): {}", code, description);
		}
	}

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		ScreenRect ToScreenRect(const RECT& rect)
		{
			return ScreenRect{rect.left, rect.top, rect.right, rect.bottom};
		}

		BOOL CALLBACK CollectWorkArea(HMONITOR monitor, HDC, LPRECT, LPARAM data)
		{
			MONITORINFO info{};
			info.cbSize = sizeof(info);

			if (GetMonitorInfoW(monitor, &info) != 0)
			{
				reinterpret_cast<std::vector<ScreenRect>*>(data)->push_back(ToScreenRect(info.rcWork));
			}

			return TRUE;
		}

		std::vector<ScreenRect> MonitorWorkAreas()
		{
			std::vector<ScreenRect> areas;
			EnumDisplayMonitors(nullptr, nullptr, &CollectWorkArea, reinterpret_cast<LPARAM>(&areas));
			return areas;
		}

		MONITORINFO PrimaryMonitor()
		{
			MONITORINFO info{};
			info.cbSize = sizeof(info);
			GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
			return info;
		}
	}
#endif

	GlfwWindow::GlfwWindow(GLFWwindow* handle) : m_Handle(handle) {}

	namespace {
		// GLFW hands back only the window pointer, so the callback has to find its way home
		// through the user pointer set in InstallCallbacks.
		GlfwWindow* WindowFrom(GLFWwindow* handle)
		{
			return static_cast<GlfwWindow*>(glfwGetWindowUserPointer(handle));
		}

		KeyModifier ToModifiers(int mods)
		{
			KeyModifier result = KeyModifier::None;

			if ((mods & GLFW_MOD_SHIFT) != 0)
			{
				result = result | KeyModifier::Shift;
			}
			if ((mods & GLFW_MOD_CONTROL) != 0)
			{
				result = result | KeyModifier::Control;
			}
			if ((mods & GLFW_MOD_ALT) != 0)
			{
				result = result | KeyModifier::Alt;
			}
			if ((mods & GLFW_MOD_SUPER) != 0)
			{
				result = result | KeyModifier::Super;
			}

			return result;
		}
	}

	void GlfwWindow::InstallCallbacks()
	{
		glfwSetWindowUserPointer(m_Handle, this);

		glfwSetWindowSizeCallback(m_Handle,
								  [](GLFWwindow* handle, int width, int height) {
									  GlfwWindow* self = WindowFrom(handle);
									  if (self == nullptr || !self->m_EventCallback)
									  {
										  return;
									  }

									  WindowResizeEvent event{width, height};
									  self->m_EventCallback(event);
								  });

		glfwSetWindowCloseCallback(m_Handle,
								   [](GLFWwindow* handle) {
									   GlfwWindow* self = WindowFrom(handle);
									   if (self == nullptr || !self->m_EventCallback)
									   {
										   return;
									   }

									   WindowCloseEvent event;
									   self->m_EventCallback(event);
								   });

		glfwSetWindowFocusCallback(m_Handle,
								   [](GLFWwindow* handle, int focused) {
									   GlfwWindow* self = WindowFrom(handle);
									   if (self == nullptr || !self->m_EventCallback)
									   {
										   return;
									   }

									   if (focused == GLFW_TRUE)
									   {
										   WindowFocusEvent event;
										   self->m_EventCallback(event);
									   }
									   else
									   {
										   WindowLostFocusEvent event;
										   self->m_EventCallback(event);
									   }
								   });

		glfwSetWindowPosCallback(m_Handle,
								 [](GLFWwindow* handle, int x, int y) {
									 GlfwWindow* self = WindowFrom(handle);
									 if (self == nullptr || !self->m_EventCallback)
									 {
										 return;
									 }

									 WindowMovedEvent event{x, y};
									 self->m_EventCallback(event);
								 });

		glfwSetKeyCallback(m_Handle,
						   [](GLFWwindow* handle, int key, int, int action, int mods) {
							   GlfwWindow* self = WindowFrom(handle);
							   if (self == nullptr || !self->m_EventCallback)
							   {
								   return;
							   }

							   const KeyCode code = static_cast<KeyCode>(key);
							   const KeyModifier modifiers = ToModifiers(mods);

							   switch (action)
							   {
							   case GLFW_PRESS:
							   {
								   KeyPressedEvent event{code, modifiers, false};
								   self->m_EventCallback(event);
								   break;
							   }
							   case GLFW_REPEAT:
							   {
								   KeyPressedEvent event{code, modifiers, true};
								   self->m_EventCallback(event);
								   break;
							   }
							   case GLFW_RELEASE:
							   {
								   KeyReleasedEvent event{code, modifiers};
								   self->m_EventCallback(event);
								   break;
							   }
							   default:
								   break;
							   }
						   });

		glfwSetCharCallback(m_Handle,
							[](GLFWwindow* handle, unsigned int codepoint) {
								GlfwWindow* self = WindowFrom(handle);
								if (self == nullptr || !self->m_EventCallback)
								{
									return;
								}

								KeyTypedEvent event{codepoint};
								self->m_EventCallback(event);
							});

		glfwSetMouseButtonCallback(m_Handle,
								   [](GLFWwindow* handle, int button, int action, int) {
									   GlfwWindow* self = WindowFrom(handle);
									   if (self == nullptr || !self->m_EventCallback)
									   {
										   return;
									   }

									   const MouseCode code = static_cast<MouseCode>(button);

									   if (action == GLFW_PRESS)
									   {
										   MouseButtonPressedEvent event{code};
										   self->m_EventCallback(event);
									   }
									   else if (action == GLFW_RELEASE)
									   {
										   MouseButtonReleasedEvent event{code};
										   self->m_EventCallback(event);
									   }
								   });

		glfwSetScrollCallback(m_Handle,
							  [](GLFWwindow* handle, double xOffset, double yOffset) {
								  GlfwWindow* self = WindowFrom(handle);
								  if (self == nullptr || !self->m_EventCallback)
								  {
									  return;
								  }

								  MouseScrolledEvent event{static_cast<float>(xOffset),
														   static_cast<float>(yOffset)};
								  self->m_EventCallback(event);
							  });

		glfwSetCursorPosCallback(m_Handle,
								 [](GLFWwindow* handle, double x, double y) {
									 GlfwWindow* self = WindowFrom(handle);
									 if (self == nullptr || !self->m_EventCallback)
									 {
										 return;
									 }

									 MouseMovedEvent event{static_cast<float>(x), static_cast<float>(y)};
									 self->m_EventCallback(event);
								 });

		// GLFW hands the dropped paths over as UTF-8 whatever the platform stores them as, so
		// they go back through FromUtf8 rather than std::filesystem::path's own constructor -
		// which would read them in the machine's ANSI codepage and mangle every path outside it.
		//
		// The ImGui GLFW backend installs no drop callback of its own, so this one survives its
		// initialisation even though it runs after the window is built.
		glfwSetDropCallback(m_Handle,
							[](GLFWwindow* handle, int count, const char** paths) {
								GlfwWindow* self = WindowFrom(handle);
								if (self == nullptr || !self->m_EventCallback)
								{
									return;
								}

								std::vector<std::filesystem::path> dropped;
								dropped.reserve(static_cast<std::size_t>(count));

								for (int index = 0; index < count; ++index)
								{
									dropped.push_back(FromUtf8(paths[index]));
								}

								FileDropEvent event{std::move(dropped)};
								self->m_EventCallback(event);
							});
	}

	GlfwWindow::~GlfwWindow()
	{
		// Before glfwDestroyWindow, always: the frame restores a window procedure, and a
		// destroyed window has none left to restore it to.
		m_Frame.reset();

		if (m_Handle == nullptr)
		{
			return;
		}

		glfwDestroyWindow(m_Handle);
		m_Handle = nullptr;

		if (--s_GlfwWindowCount == 0)
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
		// GLFW truncates the timeout to whole milliseconds, which wakes a hair before the
		// deadline, finds it not yet due, and spends another frame asking again. A millisecond
		// late is invisible.
		glfwWaitEventsTimeout(timeoutSeconds + 0.001);
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
		glfwSwapInterval(enabled ? 1 : 0);
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
		WINDOWPLACEMENT native{};
		native.length = sizeof(native);

		if (GetWindowPlacement(glfwGetWin32Window(m_Handle), &native) != 0)
		{
			const RECT& rect = native.rcNormalPosition;

			// A window minimized from maximized comes back maximized, and that is what the next
			// launch should do too.
			const bool maximized =
				native.showCmd == SW_SHOWMAXIMIZED ||
				(native.showCmd == SW_SHOWMINIMIZED && (native.flags & WPF_RESTORETOMAXIMIZED) != 0);

			return WindowPlacement{rect.left, rect.top, rect.right - rect.left,
								   rect.bottom - rect.top, maximized};
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
		// rcNormalPosition is in workspace coordinates, which are screen coordinates shifted by
		// a taskbar docked on the primary monitor's top or left. The monitors report screen
		// coordinates, so the fit happens there and the result is shifted back.
		const MONITORINFO primary = PrimaryMonitor();
		const int shiftX = primary.rcWork.left - primary.rcMonitor.left;
		const int shiftY = primary.rcWork.top - primary.rcMonitor.top;

		WindowPlacement fitted;

		if (placement)
		{
			WindowPlacement onScreen = *placement;
			onScreen.X += shiftX;
			onScreen.Y += shiftY;

			const std::vector<ScreenRect> areas = MonitorWorkAreas();
			fitted = FitToMonitors(onScreen, areas, ToScreenRect(primary.rcWork));
		}
		else
		{
			// Nothing saved: the size the window was created at, scaled to the monitor, which can
			// be taller than the monitor's work area leaves room for - the window then opened
			// under the taskbar. Kept clear of it and centred.
			int width = 0;
			int height = 0;
			glfwGetWindowSize(m_Handle, &width, &height);
			fitted = DefaultPlacement(width, height, ToScreenRect(primary.rcWork));
		}

		WINDOWPLACEMENT native{};
		native.length = sizeof(native);
		native.showCmd = fitted.Maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
		native.rcNormalPosition = RECT{fitted.X - shiftX, fitted.Y - shiftY,
									   fitted.X - shiftX + fitted.Width,
									   fitted.Y - shiftY + fitted.Height};

		// SetWindowPlacement shows the window itself, with showCmd.
		const HWND hwnd = glfwGetWin32Window(m_Handle);
		if (SetWindowPlacement(hwnd, &native) != 0)
		{
			// The window was created on the primary monitor, at that monitor's DPI. When the
			// placement lands it on a monitor with a different DPI, Windows sends WM_DPICHANGED
			// and the window, which scales to its monitor, resizes to the suggested rectangle:
			// the restored one scaled by the ratio of the two DPIs. A size saved at 150% would
			// come back half as large again, and grow on every launch. By now the window is at
			// the target monitor's DPI, so placing it once more is not rescaled and leaves
			// exactly the saved rectangle.
			SetWindowPlacement(hwnd, &native);
			return;
		}

		CORE_WARN("Cannot restore the window's placement (error {}), opening it at the default",
				  GetLastError());
		glfwShowWindow(m_Handle);
#else
		if (!placement)
		{
			int areaX = 0;
			int areaY = 0;
			int areaWidth = 0;
			int areaHeight = 0;
			int width = 0;
			int height = 0;

			if (GLFWmonitor* monitor = glfwGetPrimaryMonitor(); monitor != nullptr)
			{
				glfwGetMonitorWorkarea(monitor, &areaX, &areaY, &areaWidth, &areaHeight);
				glfwGetWindowSize(m_Handle, &width, &height);

				const WindowPlacement centred =
					DefaultPlacement(width, height, ScreenRect{areaX, areaY, areaX + areaWidth, areaY + areaHeight});
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
		// No frame means the caption was never reclaimed and the system owns the border. There
		// is nothing to recolour, and that is the degraded-but-working state, not an error.
		if (m_Frame != nullptr)
		{
			m_Frame->SetBorderColor(color);
		}
	}

	Result<Scope<Window>> Window::Create(const WindowSpec& spec)
	{
		if (s_GlfwWindowCount == 0)
		{
			glfwSetErrorCallback(&OnGlfwError);

			if (glfwInit() == GLFW_FALSE)
			{
				return Error{ErrorCode::Unsupported, "glfwInit failed"};
			}
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

		// Created hidden and shown once it is where it belongs, so a restored window does not
		// flash up at the default size first.
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		GLFWwindow* handle =
			glfwCreateWindow(spec.Width, spec.Height, spec.Title.c_str(), nullptr, nullptr);

		// Hints outlive the call, and ImGui's platform windows are GLFW windows too.
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

		if (handle == nullptr)
		{
			if (s_GlfwWindowCount == 0)
			{
				glfwTerminate();
			}

			return Error{ErrorCode::Unsupported, "failed to create an OpenGL 3.3 window"};
		}

		++s_GlfwWindowCount;

		auto window = CreateScope<GlfwWindow>(handle);
		window->InstallCallbacks();
		glfwMakeContextCurrent(handle);
		window->SetVSync(spec.VSync);
		LogGraphicsEnvironment();

		if (spec.CustomTitleBar)
		{
			auto frame = WindowFrame::Hide(*window, spec.BorderColor);
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