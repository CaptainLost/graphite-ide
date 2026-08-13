#include "corepch.h"

#include "Core/Command/Binding.h"

#include "Core/Base/Ascii.h"

namespace Core {

	namespace {
		constexpr char kChordSeparator = '|';

		constexpr std::string_view kUnboundText = "None";
	}

	std::string ToString(const Binding& binding)
	{
		if (binding.Primary.IsEmpty() && binding.Alternates.empty())
		{
			return std::string{kUnboundText};
		}

		std::string text = binding.Primary.ToString();

		for (const Chord& alternate : binding.Alternates)
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

		// An override binding nothing, which is a different thing from a missing override:
		// that one restores the factory chord. Matched case-insensitively, like every other
		// name in the file this comes from, so a hand-written "none" is not refused into
		// exactly the outcome the token exists to prevent.
		if (Lowered(Trimmed(text)) == Lowered(kUnboundText))
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
				binding.Primary = *chord;
				first = false;
			}
			else
			{
				binding.Alternates.push_back(*chord);
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