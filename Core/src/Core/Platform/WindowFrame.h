#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Color.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/Caption.h"

#include <memory>

namespace Core {

	class Window;

	class WindowFrame : NonCopyable
	{
	public:
		virtual ~WindowFrame() = default;

		virtual void SetCaptionMetrics(const CaptionMetrics& metrics) = 0;

		// The DWM attribute behind this may be set at any time on a live window, so a themed
		// border does not have to be decided before the window exists.
		virtual void SetBorderColor(const Color& color) = 0;

		static Result<Scope<WindowFrame>> Hide(Window& window,
											   const Color& border);
	};
}