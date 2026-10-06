#include "corepch.h"

#include "Core/Command/Binding.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"

namespace Core {

	namespace {
		constexpr char kChordSeparator = '|';

		constexpr std::string_view kUnboundText = "None";

		std::vector<std::string> ChordTextsOf(const Binding& binding)
		{
			std::vector<std::string> chordTexts;
			chordTexts.push_back(binding.Primary.ToString());

			for (const Chord& alternate : binding.Alternates)
			{
				chordTexts.push_back(alternate.ToString());
			}

			return chordTexts;
		}
	}

	std::string ToString(const Binding& binding)
	{
		const bool bindsNothing = binding.Primary.IsEmpty() && binding.Alternates.empty();
		if (bindsNothing)
		{
			return std::string{kUnboundText};
		}

		const std::vector<std::string> chordTexts = ChordTextsOf(binding);

		return JoinedWith(chordTexts, std::string_view{&kChordSeparator, 1});
	}

	std::optional<Binding> ParseBinding(std::string_view text)
	{
		const bool isUnboundToken = EqualsIgnoringCase(Trimmed(text), kUnboundText);
		if (isUnboundToken)
		{
			return Binding{};
		}

		Binding binding;
		std::size_t chordStart = 0;

		while (true)
		{
			const std::size_t separatorPosition = text.find(kChordSeparator, chordStart);
			const bool isLastChord = separatorPosition == std::string_view::npos;
			const std::string_view chordText = isLastChord ? text.substr(chordStart) : text.substr(chordStart, separatorPosition - chordStart);

			const std::optional<Chord> chord = Chord::Parse(chordText);

			if (!chord.has_value())
			{
				return std::nullopt;
			}

			const bool isPrimaryChord = chordStart == 0;
			if (isPrimaryChord)
			{
				binding.Primary = *chord;
			}
			else
			{
				binding.Alternates.push_back(*chord);
			}

			if (isLastChord)
			{
				break;
			}

			chordStart = separatorPosition + 1;
		}

		return binding;
	}
}
