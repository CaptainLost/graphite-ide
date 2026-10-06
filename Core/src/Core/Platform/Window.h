#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Color.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Events/Event.h"
#include "Core/Platform/Caption.h"
#include "Core/Platform/WindowPlacement.h"

#include <functional>

#include <memory>
#include <optional>
#include <string>

namespace Core {

	struct WindowSpec
	{
		std::string Title = "IDE";
		int Width = 1600;
		int Height = 900;
		bool VSync = true;

		bool CustomTitleBar = false;

		Color BorderColor;

		// Empty opens the window at Width x Height in the platform's default position.
		std::optional<WindowPlacement> Placement;
	};

	class Window : NonCopyable
	{
	public:
		virtual ~Window() = default;

		// Runs inside PollEvents as each event arrives. Events that arrive before there is a
		// callback are dropped, not queued, so it is set before the first poll.
		using EventCallback = std::function<void(Event&)>;
		virtual void SetEventCallback(EventCallback callback) = 0;

		virtual void PollEvents() = 0;

		// Sleeps until an event arrives, Redraw's waker fires or the timeout passes, then polls. The
		// timeout may be overrun by a millisecond but is never cut short.
		virtual void WaitEvents(double timeoutSeconds) = 0;

		virtual void SwapBuffers() = 0;

		virtual bool ShouldClose() const = 0;
		virtual void RequestClose() = 0;

		virtual void CancelClose() = 0;

		virtual void FramebufferSize(int& width, int& height) const = 0;
		virtual void SetVSync(bool enabled) = 0;

		virtual void* GetNativeHandle() const = 0;

		virtual void Minimize() = 0;

		virtual void ToggleMaximize() = 0;
		virtual bool IsMaximized() const = 0;

		// Keyboard focus of this window alone: another window of the same process holding it
		// answers false.
		virtual bool IsFocused() const = 0;

		virtual WindowPlacement GetPlacement() const = 0;

		virtual bool HasCustomTitleBar() const = 0;

		virtual void SetCaptionMetrics(const CaptionMetrics& metrics) = 0;

		virtual void SetBorderColor(const Color& color) = 0;

		static Result<Scope<Window>> Create(const WindowSpec& spec);
	};
}