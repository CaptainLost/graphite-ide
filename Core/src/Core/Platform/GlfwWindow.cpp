#include "corepch.h"

#include "Core/Platform/GlfwWindow.h"

#include "Core/Base/Log.h"

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

	Result<std::unique_ptr<Window>> Window::Create(const WindowSpec& spec)
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
			glfwCreateWindow(spec.width, spec.height, spec.title.c_str(), nullptr, nullptr);

		if (handle == nullptr)
		{
			if (s_GlfwWindowCount == 0)
				glfwTerminate();

			return Error{ErrorCode::Unsupported, "failed to create an OpenGL 3.3 window"};
		}

		++s_GlfwWindowCount;

		auto window = std::make_unique<GlfwWindow>(handle);
		glfwMakeContextCurrent(handle);
		window->SetVSync(spec.vsync);

		if (spec.customTitleBar)
		{
			auto frame = WindowFrame::Hide(*window, spec.borderColor);
			if (frame)
			{
				window->AdoptFrame(std::move(frame).GetValue());
			}
			else
			{
				CORE_WARN("Keeping the system title bar: {}", frame.GetError().message);
			}
		}

		CORE_INFO("Window created: {}x{} \"{}\"", spec.width, spec.height, spec.title);
		return Result<std::unique_ptr<Window>>{std::move(window)};
	}
}