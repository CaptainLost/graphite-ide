#include "Settings/Panel/ShortcutWhen.h"

#include "Core/Base/Strings.h"
#include "Core/Command/TypingRules.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kWaitsWhileTyping = "Not while typing";

		const char* PlaceOf(Core::TypingTarget target)
		{
			switch (target)
			{
			case Core::TypingTarget::Editor:
				return "the editor";
			case Core::TypingTarget::Terminal:
				return "the terminal";
			case Core::TypingTarget::TextField:
				return "text fields";
			case Core::TypingTarget::Nothing:
				break;
			}

			return "";
		}

		std::string DescribeChord(const Core::Chord& chord)
		{
			std::vector<std::string> waitsIn;

			for (const Core::TypingTarget target : kTypingTargets)
			{
				if (!Core::FiresIn(chord, target))
				{
					waitsIn.emplace_back(PlaceOf(target));
				}
			}

			if (waitsIn.empty())
			{
				return kFiresEverywhere;
			}

			if (waitsIn.size() == kTypingTargets.size())
			{
				return kWaitsWhileTyping;
			}

			return "Not in " + Core::JoinedWith(waitsIn, " or ");
		}

		std::string LowerFirst(std::string text)
		{
			if (!text.empty())
			{
				text.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(text.front())));
			}

			return text;
		}

		bool WaitsByDefault(const Core::Chord& chord)
		{
			const bool flagged = std::ranges::any_of(kTypingTargets, [&chord](Core::TypingTarget target) {
				return Core::FiresInFlag(chord.GetFlags(), target).has_value();
			});

			return Core::TypesCharacter(chord) && !flagged;
		}
	}

	std::string DescribeWhen(std::span<const Core::Chord> chords)
	{
		std::vector<const Core::Chord*> flaggable;

		for (const Core::Chord& chord : chords)
		{
			if (Core::HasTypingFlags(chord))
			{
				flaggable.push_back(&chord);
			}
		}

		if (flaggable.empty())
		{
			return {};
		}

		std::vector<std::string> descriptions;

		for (const Core::Chord* chord : flaggable)
		{
			descriptions.push_back(DescribeChord(*chord));
		}

		const bool allAlike = std::ranges::all_of(descriptions, [&descriptions](const std::string& description) {
			return description == descriptions.front();
		});

		if (allAlike)
		{
			return descriptions.front();
		}

		std::vector<std::string> waiting;

		for (std::size_t index = 0; index < flaggable.size(); ++index)
		{
			if (descriptions[index] != kFiresEverywhere)
			{
				waiting.push_back(std::format("{}: {}", flaggable[index]->ToString(), LowerFirst(descriptions[index])));
			}
		}

		return Core::JoinedWith(waiting, "; ");
	}

	std::string DescribeDefaultWait(std::span<const Core::Chord> chords)
	{
		std::vector<std::string> waiting;

		for (const Core::Chord& chord : chords)
		{
			if (WaitsByDefault(chord))
			{
				waiting.push_back(chord.ToString());
			}
		}

		if (waiting.empty())
		{
			return {};
		}

		if (waiting.size() == 1)
		{
			return std::format("{} types a character, so it waits while you type unless ticked here.", waiting.front());
		}

		return std::format("{} type characters, so they wait while you type unless ticked here.", Core::JoinedWith(waiting, ", "));
	}

	const char* TypingTargetLabel(Core::TypingTarget target)
	{
		switch (target)
		{
		case Core::TypingTarget::Editor:
			return "While typing in the editor";
		case Core::TypingTarget::Terminal:
			return "While typing in the terminal";
		case Core::TypingTarget::TextField:
			return "In text fields";
		case Core::TypingTarget::Nothing:
			break;
		}

		return "";
	}

	const char* TypingTargetHeading(Core::TypingTarget target)
	{
		switch (target)
		{
		case Core::TypingTarget::Editor:
			return "Editor";
		case Core::TypingTarget::Terminal:
			return "Terminal";
		case Core::TypingTarget::TextField:
			return "Text fields";
		case Core::TypingTarget::Nothing:
			break;
		}

		return "";
	}
}
