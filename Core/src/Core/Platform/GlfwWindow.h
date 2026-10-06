#pragma once

#include "Core/Base/Base.h"

#include "Core/Platform/Window.h"
#include "Core/Platform/WindowFrame.h"

#include <memory>
#include <optional>
#include <utility>

struct GLFWwindow;

namespace Core {

	class GlfwWindow final : public Window
	{
	public:
		explicit GlfwWindow(GLFWwindow* handle);
		~GlfwWindow() override;

		void SetEventCallback(EventCallback callback) override { m_EventCallback = std::move(callback); }

		// The ImGui GLFW backend chains to the callbacks it finds when it initialises, so these must be
		// installed before it: installed later, they would replace the backend's own and ImGui would
		// receive no keyboard or mouse input.
		void InstallCallbacks();

		void PollEvents() override;
		void WaitEvents(double timeoutSeconds) override;
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
		bool IsFocused() const override;
		WindowPlacement GetPlacement() const override;

		// Call after AdoptFrame: the frame changes the non-client area, and a placement applied
		// before it would be measured against the old one.
		void Show(const std::optional<WindowPlacement>& placement);

		bool HasCustomTitleBar() const override;
		void SetCaptionMetrics(const CaptionMetrics& metrics) override;
		void SetBorderColor(const Color& color) override;

		void AdoptFrame(Scope<WindowFrame> frame) { m_Frame = std::move(frame); }

	private:
		static GlfwWindow* ListeningWindowOf(GLFWwindow* handle);

		static void OnResized(GLFWwindow* handle, int width, int height);
		static void OnCloseRequested(GLFWwindow* handle);
		static void OnFocusChanged(GLFWwindow* handle, int focused);
		static void OnMoved(GLFWwindow* handle, int x, int y);
		static void OnKey(GLFWwindow* handle, int key, int scancode, int action, int modifierBits);
		static void OnCharacterTyped(GLFWwindow* handle, unsigned int codepoint);
		static void OnMouseButton(GLFWwindow* handle, int button, int action, int modifierBits);
		static void OnScrolled(GLFWwindow* handle, double xOffset, double yOffset);
		static void OnCursorMoved(GLFWwindow* handle, double x, double y);
		static void OnFilesDropped(GLFWwindow* handle, int pathCount, const char** paths);

		GLFWwindow* m_Handle = nullptr;
		EventCallback m_EventCallback;

		Scope<WindowFrame> m_Frame;
	};
}
