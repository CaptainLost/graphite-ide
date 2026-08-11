#pragma once

#include "Core/Base/Color.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/Caption.h"

#include <cstdint>
#include <memory>

namespace core
{
    class Window;

    /// Packs a colour into the 0x00BBGGRR integer the system's window attributes take —
    /// a Win32 COLORREF, spelled without windows.h so it is one pure function that builds
    /// and is tested everywhere. Alpha is dropped: the border DWM draws has none.
    ///
    /// Channels are rounded to nearest, not truncated. A theme writes them to three
    /// decimals, so 0.247 is 62.985 of 255 and truncation would hand the system 62 —
    /// a colour one step off the one every panel is outlined with, for no reason a
    /// reader could see. Anything outside [0, 1] clamps to the end it passed.
    [[nodiscard]] std::uint32_t PackBgr(const Color& color);

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
