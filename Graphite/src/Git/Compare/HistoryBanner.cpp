#include "Git/Compare/HistoryBanner.h"

namespace Graphite {

	namespace {

		std::string OldCopyBanner(const VsInput& input, const Core::OldCopy& copy)
		{
			const bool single = input.Distance.has_value() && input.Distance->Behind == 1;

			return "The default branch of " + input.Read.Refs[copy.Ref].Remote +
				   ", made when the fork was. It is an old copy of " + LineNameOf(input.Read, copy.Of) + ", " +
				   BehindText(input.Distance) + (single ? " commit" : " commits") +
				   " behind it, so no branch is compared with it.";
		}

		std::optional<std::string> BranchBanner(const VsInput& input, const Core::BranchLine& line)
		{
			switch (line.State)
			{
			case Core::BranchState::HistoryCutOff:
				return "History cut off here: this clone is shallow, so the commit it shares with its line is not on this computer.";
			case Core::BranchState::NoCommonHistory:
				return "No commit in common with any line.";
			case Core::BranchState::NoOwnCommits:
			case Core::BranchState::MergedByHistory:
				break;
			case Core::BranchState::NotMerged:
			case Core::BranchState::NotCompared:
			case Core::BranchState::TipMissing:
				return std::nullopt;
			}

			if (!line.ComparedWith.has_value())
			{
				return std::nullopt;
			}

			const std::string name = LineNameOf(input.Read, *line.ComparedWith);

			if (line.State == Core::BranchState::NoOwnCommits)
			{
				return "No commits of its own yet: it points at " + input.Read.Refs[line.Ref].Tip.ShortHex() + " on " + name +
					   ". Shown, never hidden as merged, until it gets one.";
			}

			return "Merged into " + name + ": its commits are in " + name + "'s history. The newer commits of " + name +
				   " are dimmed.";
		}
	}

	std::optional<std::string> HistoryBannerFor(const VsInput& input)
	{
		const std::string_view shown = input.Lines.ShownAs(input.ShownRef);

		// A base or an old copy the user picked a line for is folded against it as a branch is, with no state of the
		// model's to tell, and the distance given is the pick's.
		if (input.Lines.PickedLineOf(shown).has_value())
		{
			return std::nullopt;
		}

		if (const Core::OldCopy* const copy = input.Lines.OldCopyNamed(shown); copy != nullptr)
		{
			return OldCopyBanner(input, *copy);
		}

		const Core::BranchLine* const line = input.Lines.LineOf(shown);

		return line != nullptr ? BranchBanner(input, *line) : std::nullopt;
	}
}
