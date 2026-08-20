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

		// Must run before the GUI backend initialises: that backend chains to whatever
		// callbacks it finds, so these have to be there first or they are replaced.
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
		void SetBorderColor(const Color& color) override;

		void AdoptFrame(Scope<WindowFrame> frame) { m_Frame = std::move(frame); }

	private:
		GLFWwindow* m_Handle = nullptr;
		EventCallbackFn m_EventCallback;

		Scope<WindowFrame> m_Frame;
	};
}