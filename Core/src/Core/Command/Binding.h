#pragma once

#include "Core/Command/Chord.h"

#include <vector>

namespace core
{
    /// One command's triggers: the chord the menu shows, plus every other way of
    /// reaching it. Zoom In is Ctrl+= with Ctrl+Num+ and Ctrl+wheel behind it.
    struct Binding
    {
        Chord primary;
        std::vector<Chord> alternates;
    };
}
