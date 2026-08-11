#pragma once

namespace core
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
}
