#pragma once

#include <string_view>

namespace graphite
{
    /// How the file tree presents entries that git ignores.
    enum class GitIgnoredDisplay
    {
        Show,   ///< Draw them like any other entry.
        Dim,    ///< Draw them in the disabled colour — the default.
        Hide    ///< Leave them out of the tree.
    };

    /// The token written to `graphite.json`. Deliberately not the enumerator name, so the
    /// stored file does not change meaning if the enumerators are ever reordered.
    [[nodiscard]] constexpr std::string_view GitIgnoredDisplayToString(GitIgnoredDisplay display)
    {
        switch (display)
        {
        case GitIgnoredDisplay::Show: return "show";
        case GitIgnoredDisplay::Hide: return "hide";
        case GitIgnoredDisplay::Dim:  break;
        }

        return "dim";
    }

    /// Anything unrecognised — including the empty string on first run — falls back to
    /// Dim, the mode that hides nothing while still showing the status.
    [[nodiscard]] constexpr GitIgnoredDisplay GitIgnoredDisplayFromString(std::string_view text)
    {
        if (text == "show")
        {
            return GitIgnoredDisplay::Show;
        }

        if (text == "hide")
        {
            return GitIgnoredDisplay::Hide;
        }

        return GitIgnoredDisplay::Dim;
    }
}
