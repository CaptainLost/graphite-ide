#pragma once

#include "Core/Base/Base.h"

#include "Core/Platform/Window.h"
#include "Core/Platform/WindowFrame.h"

#include <memory>
#include <utility>

struct GLFWwindow;

namespace Core {

	class GlfwWindow final : public Window
	{
	public:
		explicit GlfwWindow(GLFWwindow* handle);
		~GlfwWindow() override;

		void SetEventCallback(EventCallbackFn callback) override { m_EventCallback = std::move(callback); }

		// Installs the GLFW callbacks that turn window and input notifications into events.
		// Must run before the GUI backend initialises: that backend installs callbacks of
		// its own and chains to whatever it found, so ours have to be there first or they
		// are replaced rather than chained.
		void InstallCallbacks();

		void PollEvents() override;
		void SwapBuffers() override;

		bool ShouldClose() const override;
		void RequestClose() override;
		void CancelClose() override;

		void FramebufferSize(int& width, int& height) const override;
		void SetVSync(bool enabled) override;

		void* GetNativeHandle() const override { return m_Handle; }

		void Minimize() override;
		void ToggleMaximize() override;
		bool IsMaximized() const override;
		bool HasCustomTitleBar() const override;
		void SetCaptionMetrics(const CaptionMetrics& metrics) override;

		void AdoptFrame(Scope<WindowFrame> frame) { m_Frame = std::move(frame); }

	private:
		GLFWwindow* m_Handle = nullptr;
		EventCallbackFn m_EventCallback;

		Scope<WindowFrame> m_Frame;
	};
}