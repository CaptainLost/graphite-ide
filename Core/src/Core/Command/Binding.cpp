#include "corepch.h"

#include "Core/Command/Binding.h"

#include "Core/Base/Ascii.h"

namespace core
{
    namespace
    {
        /// Primary first, alternates after, '|' between. The separator cannot occur in a
        /// chord's own text, which is made of modifier and key names joined by '+'.
        constexpr char kChordSeparator = '|';

        /// What a command the user took every chord away from is written as. The empty
        /// string cannot carry it — ParseBinding refuses empty text, so an unbound
        /// override would be dropped on the next start and the factory chord would come
        /// back, silently undoing the very conflict the rebinding screen resolved. No key
        /// is named "None", so this cannot collide with a chord's own text.
        constexpr std::string_view kUnboundText = "None";
    }

    std::string ToString(const Binding& binding)
    {
        if (binding.primary.IsEmpty() && binding.alternates.empty())
        {
            return std::string{kUnboundText};
        }

        std::string text = binding.primary.ToString();

        for (const Chord& alternate : binding.alternates)
        {
            text += kChordSeparator;
            text += alternate.ToString();
        }

        return text;
    }

    std::optional<Binding> ParseBinding(std::string_view text)
    {
        Binding binding;
        bool first = true;
        std::size_t start = 0;

        // An override that binds nothing, which is a different thing from a missing
        // override: the latter restores the factory chord.
        // Case-insensitively, like Chord::Parse reads every other name in the file this
        // comes from: a hand-written "none" must not be refused and silently restore the
        // factory chord, which is the one failure this token exists to prevent.
        if (ascii::Lowered(ascii::Trimmed(text)) == ascii::Lowered(kUnboundText))
        {
            return binding;
        }

        for (;;)
        {
            const std::size_t separator = text.find(kChordSeparator, start);
            const std::string_view part =
                text.substr(start, separator == std::string_view::npos
                                       ? std::string_view::npos
                                       : separator - start);

            const std::optional<Chord> chord = Chord::Parse(part);

            if (!chord.has_value())
            {
                return std::nullopt;
            }

            if (first)
            {
                binding.primary = *chord;
                first = false;
            }
            else
            {
                binding.alternates.push_back(*chord);
            }

            if (separator == std::string_view::npos)
            {
                break;
            }

            start = separator + 1;
        }

        return binding;
    }
}
