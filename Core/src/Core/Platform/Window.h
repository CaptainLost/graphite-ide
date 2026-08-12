#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Color.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/Caption.h"

#include <memory>
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
	};

	class Window : NonCopyable
	{
	public:
		virtual ~Window() = default;

		virtual void PollEvents() = 0;
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

		virtual bool HasCustomTitleBar() const = 0;

		virtual void SetCaptionMetrics(const CaptionMetrics& metrics) = 0;

		static Result<Scope<Window>> Create(const WindowSpec& spec);
	};
}