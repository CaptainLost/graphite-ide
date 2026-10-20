#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Color.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/Caption.h"

#include <memory>

namespace Core {

	class Window;

	enum class FrameBorder
	{
		Inset,
		Flush
	};

	class WindowFrame : NonCopyable
	{
	public:
		virtual ~WindowFrame() = default;

		virtual void SetCaptionMetrics(const CaptionMetrics& metrics) = 0;

		// DWM takes a border colour at any time on a live window, so a theme can change it after the
		// window exists.
		virtual void SetBorderColor(const Color& color) = 0;

		static Result<Scope<WindowFrame>> Hide(Window& window,
											   const Color& borderColor);

		static Result<Scope<WindowFrame>> HideCaptionOf(void* nativeWindow, const Color& borderColor,
																FrameBorder border = FrameBorder::Inset);
	};
}