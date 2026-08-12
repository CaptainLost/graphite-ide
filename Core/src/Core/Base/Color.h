#pragma once

#include <cstdint>

namespace Core
{
    /// A colour as four straight-alpha channels in [0, 1] — the same numbers ImGui's
    /// ImVec4 carries, in a type the layers below Core::Gui can hold. It lives in
    /// Core::Base because a colour belongs to no layer in particular: the theme names
    /// them, the platform hands one to the system, and neither should have to include
    /// the other's headers to say the word.
    struct Color
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    /// Packs a colour into the 0x00BBGGRR integer the Win32 window attributes take — a
    /// COLORREF, spelled without windows.h so it is one pure function that builds and is
    /// tested everywhere, and so the layer that needs it does not have to own it. Alpha
    /// is dropped: the border DWM draws with this has none.
    ///
    /// Channels are rounded to nearest, not truncated. A theme writes them to three
    /// decimals, so 0.247 is 62.985 of 255 and truncation would hand the system 62 —
    /// a colour one step off the one every panel is outlined with, for no reason a
    /// reader could see. Anything outside [0, 1] clamps to the end it passed.
    [[nodiscard]] std::uint32_t PackBgr(const Color& color);
}
