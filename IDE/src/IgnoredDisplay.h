#pragma once

#include <string_view>

namespace ide
{
    /// How the file tree presents entries that git ignores.
    enum class IgnoredDisplay
    {
        Show,   ///< Draw them like any other entry.
        Dim,    ///< Draw them in the disabled colour — the default.
        Hide    ///< Leave them out of the tree.
    };

    /// The token written to `ide.ini`. Deliberately not the enumerator name, so the
    /// stored file does not change meaning if the enumerators are ever reordered.
    [[nodiscard]] constexpr std::string_view IgnoredDisplayToString(IgnoredDisplay display)
    {
        switch (display)
        {
        case IgnoredDisplay::Show: return "show";
        case IgnoredDisplay::Hide: return "hide";
        case IgnoredDisplay::Dim:  break;
        }

        return "dim";
    }

    /// Anything unrecognised — including the empty string on first run — falls back to
    /// Dim, the mode that hides nothing while still showing the status.
    [[nodiscard]] constexpr IgnoredDisplay IgnoredDisplayFromString(std::string_view text)
    {
        if (text == "show")
            return IgnoredDisplay::Show;

        if (text == "hide")
            return IgnoredDisplay::Hide;

        return IgnoredDisplay::Dim;
    }
}
