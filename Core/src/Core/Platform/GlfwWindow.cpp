#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/Platform/GlfwWindow.h"

#include "Core/Base/Log.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"

#include <GLFW/glfw3.h>

namespace Core {

	namespace {
		int s_GlfwWindowCount = 0;

		void OnGlfwError(int code, const char* description)
		{
			CORE_ERROR("GLFW ({}): {}", code, description);
		}
	}

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
				result = result | KeyModifier::Shift;
			if ((mods & GLFW_MOD_CONTROL) != 0)
				result = result | KeyModifier::Control;
			if ((mods & GLFW_MOD_ALT) != 0)
				result = result | KeyModifier::Alt;
			if ((mods & GLFW_MOD_SUPER) != 0)
				result = result | KeyModifier::Super;

			return result;
		}
	}

	void GlfwWindow::InstallCallbacks()
	{
		glfwSetWindowUserPointer(m_Handle, this);

		glfwSetWindowSizeCallback(m_Handle,
								  [](GLFWwindow* handle, int width, int height)
								  {
									  GlfwWindow* self = WindowFrom(handle);
									  if (self == nullptr || !self->m_EventCallback)
										  return;

									  WindowResizeEvent event{width, height};
									  self->m_EventCallback(event);
								  });

		glfwSetWindowCloseCallback(m_Handle,
								   [](GLFWwindow* handle)
								   {
									   GlfwWindow* self = WindowFrom(handle);
									   if (self == nullptr || !self->m_EventCallback)
										   return;

									   WindowCloseEvent event;
									   self->m_EventCallback(event);
								   });

		glfwSetWindowFocusCallback(m_Handle,
								   [](GLFWwindow* handle, int focused)
								   {
									   GlfwWindow* self = WindowFrom(handle);
									   if (self == nullptr || !self->m_EventCallback)
										   return;

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
								 [](GLFWwindow* handle, int x, int y)
								 {
									 GlfwWindow* self = WindowFrom(handle);
									 if (self == nullptr || !self->m_EventCallback)
										 return;

									 WindowMovedEvent event{x, y};
									 self->m_EventCallback(event);
								 });

		glfwSetKeyCallback(m_Handle,
						   [](GLFWwindow* handle, int key, int, int action, int mods)
						   {
							   GlfwWindow* self = WindowFrom(handle);
							   if (self == nullptr || !self->m_EventCallback)
								   return;

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
							[](GLFWwindow* handle, unsigned int codepoint)
							{
								GlfwWindow* self = WindowFrom(handle);
								if (self == nullptr || !self->m_EventCallback)
									return;

								KeyTypedEvent event{codepoint};
								self->m_EventCallback(event);
							});

		glfwSetMouseButtonCallback(m_Handle,
								   [](GLFWwindow* handle, int button, int action, int)
								   {
									   GlfwWindow* self = WindowFrom(handle);
									   if (self == nullptr || !self->m_EventCallback)
										   return;

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
							  [](GLFWwindow* handle, double xOffset, double yOffset)
							  {
								  GlfwWindow* self = WindowFrom(handle);
								  if (self == nullptr || !self->m_EventCallback)
									  return;

								  MouseScrolledEvent event{static_cast<float>(xOffset),
														   static_cast<float>(yOffset)};
								  self->m_EventCallback(event);
							  });

		glfwSetCursorPosCallback(m_Handle,
								 [](GLFWwindow* handle, double x, double y)
								 {
									 GlfwWindow* self = WindowFrom(handle);
									 if (self == nullptr || !self->m_EventCallback)
										 return;

									 MouseMovedEvent event{static_cast<float>(x), static_cast<float>(y)};
									 self->m_EventCallback(event);
								 });
	}

	GlfwWindow::~GlfwWindow()
	{
		// Before glfwDestroyWindow, always: the frame restores a window procedure, and a
		// destroyed window has none left to restore it to.
		m_Frame.reset();

		if (m_Handle == nullptr)
			return;

		glfwDestroyWindow(m_Handle);
		m_Handle = nullptr;

		if (--s_GlfwWindowCount == 0)
			glfwTerminate();
	}

	void GlfwWindow::PollEvents()
	{
		glfwPollEvents();
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

	Result<Scope<Window>> Window::Create(const WindowSpec& spec)
	{
		if (s_GlfwWindowCount == 0)
		{
			glfwSetErrorCallback(&OnGlfwError);

			if (glfwInit() == GLFW_FALSE)
				return Error{ErrorCode::Unsupported, "glfwInit failed"};
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

		GLFWwindow* handle =
			glfwCreateWindow(spec.Width, spec.Height, spec.Title.c_str(), nullptr, nullptr);

		if (handle == nullptr)
		{
			if (s_GlfwWindowCount == 0)
				glfwTerminate();

			return Error{ErrorCode::Unsupported, "failed to create an OpenGL 3.3 window"};
		}

		++s_GlfwWindowCount;

		auto window = CreateScope<GlfwWindow>(handle);
		window->InstallCallbacks();
		glfwMakeContextCurrent(handle);
		window->SetVSync(spec.VSync);

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

		CORE_INFO("Window created: {}x{} \"{}\"", spec.Width, spec.Height, spec.Title);
		return Result<Scope<Window>>{std::move(window)};
	}
}