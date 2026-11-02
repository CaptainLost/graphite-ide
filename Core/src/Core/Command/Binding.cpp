#include "corepch.h"

#include "Core/Command/Binding.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"

namespace Core {

	namespace {
		constexpr char kChordSeparator = '|';

		constexpr std::string_view kUnboundText = "None";

		constexpr char kFlagsOpen = '{';
		constexpr char kFlagsClose = '}';
		constexpr char kFlagSeparator = ' ';
		constexpr char kFiresSign = '+';
		constexpr char kWaitsSign = '-';

		struct FlagName
		{
			std::optional<bool> ChordFlags::*Flag;
			std::string_view Name;
		};

		constexpr FlagName kFlagNames[] = {
			{.Flag = &ChordFlags::FiresInEditor, .Name = "editor"},
			{.Flag = &ChordFlags::FiresInTerminal, .Name = "terminal"},
			{.Flag = &ChordFlags::FiresInTextFields, .Name = "fields"},
			{.Flag = &ChordFlags::RepeatsWhileHeld, .Name = "repeat"},
		};

		std::string FlagsTextOf(const ChordFlags& flags)
		{
			std::vector<std::string> tokens;

			for (const FlagName& flagName : kFlagNames)
			{
				if (const std::optional<bool>& flag = flags.*flagName.Flag; flag.has_value())
				{
					tokens.push_back((*flag ? kFiresSign : kWaitsSign) + std::string{flagName.Name});
				}
			}

			return std::string{kFlagsOpen} + JoinedWith(tokens, std::string_view{&kFlagSeparator, 1}) + kFlagsClose;
		}

		std::string ChordTextOf(const Chord& chord)
		{
			std::string text = chord.ToString();

			if (!chord.GetFlags().IsEmpty())
			{
				text += kFlagSeparator;
				text += FlagsTextOf(chord.GetFlags());
			}

			return text;
		}

		std::vector<std::string> ChordTextsOf(const Binding& binding)
		{
			std::vector<std::string> chordTexts;
			chordTexts.push_back(ChordTextOf(binding.Primary));

			for (const Chord& alternate : binding.Alternates)
			{
				chordTexts.push_back(ChordTextOf(alternate));
			}

			return chordTexts;
		}

		bool ReadFlag(std::string_view token, ChordFlags& flags)
		{
			if (token.size() < 2)
			{
				return false;
			}

			const char sign = token.front();
			const bool knownSign = sign == kFiresSign || sign == kWaitsSign;
			const std::string_view name = token.substr(1);

			for (const FlagName& flagName : kFlagNames)
			{
				if (knownSign && EqualsIgnoringCase(name, flagName.Name))
				{
					flags.*flagName.Flag = sign == kFiresSign;
					return true;
				}
			}

			return false;
		}

		std::optional<ChordFlags> ReadFlags(std::string_view text)
		{
			ChordFlags flags;

			for (const std::string& token : SplitSkippingEmpty(text, kFlagSeparator))
			{
				if (!ReadFlag(Trimmed(token), flags))
				{
					return std::nullopt;
				}
			}

			return flags;
		}

		// A chord is its gesture, then its flags in braces if the user set any: "F {-editor +terminal}".
		std::optional<Chord> ReadChord(std::string_view text)
		{
			const std::size_t flagsStart = text.find(kFlagsOpen);

			if (flagsStart == std::string_view::npos)
			{
				return Chord::Parse(text);
			}

			const std::string flagsText = Trimmed(text.substr(flagsStart + 1));

			if (flagsText.empty() || flagsText.back() != kFlagsClose)
			{
				return std::nullopt;
			}

			const std::optional<Chord> chord = Chord::Parse(text.substr(0, flagsStart));
			const std::optional<ChordFlags> flags = ReadFlags(std::string_view(flagsText).substr(0, flagsText.size() - 1));

			if (!chord.has_value() || !flags.has_value())
			{
				return std::nullopt;
			}

			return chord->WithFlags(*flags);
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

			const std::optional<Chord> chord = ReadChord(chordText);

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
