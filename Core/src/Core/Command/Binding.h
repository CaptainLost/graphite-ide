#pragma once

#include "Core/Command/Chord.h"

#include <optional>
#include <string>
#include <string_view>
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

    /// Primary first, alternates after, '|' between; "None" for a binding that reaches
    /// nothing. This is the text a stored binding is written as, so it lives beside the
    /// type it describes rather than beside whichever file happens to store it.
    [[nodiscard]] std::string ToString(const Binding& binding);

    /// Empty when any part of the text names a chord this build does not know, so a
    /// half-understood binding is refused whole rather than applied in part. An empty
    /// text is a malformed entry too, not a binding of nothing. Reports rather than
    /// asserts: a hand-edited settings file reaches this.
    [[nodiscard]] std::optional<Binding> ParseBinding(std::string_view text);
}
