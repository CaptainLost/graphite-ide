#pragma once

#include <cstdint>

namespace Core
{
    /// Where a chord counts. Global chords are routed once per frame after every window
    /// has closed; a scoped chord is fired by whoever owns the scope, from inside the
    /// window that defines it. Ctrl+wheel counts only over a hovered editor — zooming
    /// the code from over the file tree would be a change of behaviour, not a refactor.
    enum class Scope : std::uint8_t
    {
        Global,
        HoveredEditor
    };
}
