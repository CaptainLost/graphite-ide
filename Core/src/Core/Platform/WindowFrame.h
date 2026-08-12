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

		static Result<Scope<WindowFrame>> Hide(Window& window,
														 const Color& border);
	};
}