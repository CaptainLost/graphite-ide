#pragma once

#include "Core/Base/Color.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/Caption.h"

#include <memory>

namespace Core
{
    class Window;

    /// Takes the system title bar off a window that already exists, and answers the
    /// system's hit tests from what the application says it drew. Destroying it puts the
    /// window back the way it was found.
    class WindowFrame : NonCopyable
    {
    public:
        virtual ~WindowFrame() = default;

        virtual void SetCaptionMetrics(const CaptionMetrics& metrics) = 0;

        /// Unsupported on a platform with no implementation, which leaves `window`
        /// untouched and its system bar in place. `border` is the colour the frame asks
        /// the system to outline the window with, passed in rather than looked up: it is
        /// the theme's, and the theme lives above this layer — see Core/Gui/Theme.h.
        [[nodiscard]] static Result<std::unique_ptr<WindowFrame>> Hide(Window& window,
                                                                      const Color& border);
    };
}
