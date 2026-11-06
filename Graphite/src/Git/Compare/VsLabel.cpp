#include "Git/Compare/VsLabel.h"

#include "Core/Base/Count.h"

#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kPending = "\xE2\x80\xA6";

		VsLabel Plain(std::string lead)
		{
			return VsLabel{.Lead = std::move(lead), .Line = {}, .Counts = {}, .How = {}, .Mark = std::nullopt};
		}

		bool IsGuessedMainLine(const LineRead& read, std::size_t ref)
		{
			const std::vector<Core::LineBase>& bases = read.Model.Bases;

			return !bases.empty() && bases.front().Ref == ref && bases.front().Reason == Core::BaseReason::MainLineGuess;
		}

		std::optional<std::string> MarkOf(const LineRead& read, const Core::BranchLine& line)
		{
			if (line.ByAnswer)
			{
				return "your pick";
			}

			if (line.ComparedWith.has_value() && IsGuessedMainLine(read, *line.ComparedWith))
			{
				return "guessed";
			}

			return std::nullopt;
		}

		VsLabel DescribeBranch(const VsInput& input, const Core::BranchLine& line)
		{
			switch (line.State)
			{
			case Core::BranchState::TipMissing:
				return Plain("tip missing");
			case Core::BranchState::HistoryCutOff:
				return Plain("history cut off");
			case Core::BranchState::NoCommonHistory:
				return Plain("no common history");
			case Core::BranchState::NoOwnCommits:
			case Core::BranchState::MergedByHistory:
			case Core::BranchState::NotMerged:
			case Core::BranchState::NotCompared:
				break;
			}

			VsLabel label;
			label.Mark = MarkOf(input.Read, line);

			if (!line.ComparedWith.has_value() || line.Kind == Core::ComparedKind::None ||
				line.State == Core::BranchState::NotCompared)
			{
				label.Lead = "compared with nothing";
				return label;
			}

			label.Line = LineNameOf(input.Read, *line.ComparedWith);

			if (line.State == Core::BranchState::NoOwnCommits)
			{
				label.Lead = "no own commits \xC2\xB7 points into";
			}
			else if (line.State == Core::BranchState::MergedByHistory)
			{
				label.Lead = "merged into";
			}
			else if (line.State == Core::BranchState::NotMerged && IsMergedByContent(input.Content))
			{
				label.Lead = "merged into";
				label.How = input.Content->Kind == Core::ContentKind::Squash ? "squash" : "rebase";
			}
			else
			{
				label.Lead = line.Kind == Core::ComparedKind::Branch ? "on top of" : "vs";
				label.Counts = Core::FormatCount(line.Ahead) + " ahead, " + BehindText(input.Distance) + " behind";
			}

			return label;
		}

		// A base or an old copy the user picked a line for: the model counts nothing for it, so both counts are the
		// distance's.
		VsLabel DescribePick(const VsInput& input, std::size_t picked)
		{
			const std::string ahead =
				input.Distance.has_value() ? Core::FormatCount(input.Distance->Ahead) : std::string{kPending};

			VsLabel label;
			label.Lead = "vs";
			label.Line = LineNameOf(input.Read, picked);
			label.Counts = ahead + " ahead, " + BehindText(input.Distance) + " behind";
			label.Mark = "your pick";

			return label;
		}
	}

	bool IsMergedByContent(const std::optional<Core::ContentVerdict>& verdict)
	{
		return verdict.has_value() && verdict->Outcome == Core::ContentOutcome::Merged;
	}

	std::string LineNameOf(const LineRead& read, std::size_t index)
	{
		const Core::LineRef& ref = read.Refs[index];

		return ref.Remote.empty() ? ref.ShortName : ref.Remote + "/" + ref.ShortName;
	}

	std::string BehindText(const std::optional<Core::LineDistance>& distance)
	{
		return distance.has_value() ? Core::FormatCount(distance->Behind) : std::string{kPending};
	}

	VsLabel DescribeVs(const VsInput& input)
	{
		const std::string_view shown = input.Lines.ShownAs(input.ShownRef);

		if (const std::optional<std::size_t> picked = input.Lines.PickedLineOf(shown))
		{
			return DescribePick(input, *picked);
		}

		if (input.Lines.BaseNamed(shown) != nullptr)
		{
			return Plain("compare with\xE2\x80\xA6");
		}

		if (input.Lines.OldCopyNamed(shown) != nullptr)
		{
			return Plain("compared with nothing");
		}

		const Core::BranchLine* const line = input.Lines.LineOf(shown);

		return line != nullptr ? DescribeBranch(input, *line) : VsLabel{};
	}

	std::string HeadOperationText(Core::RepositoryOperation operation)
	{
		switch (operation)
		{
		case Core::RepositoryOperation::None:
			return {};
		case Core::RepositoryOperation::Merge:
			return "merge in progress";
		case Core::RepositoryOperation::Rebase:
			return "rebase in progress";
		case Core::RepositoryOperation::CherryPick:
			return "cherry-pick in progress";
		case Core::RepositoryOperation::Revert:
			return "revert in progress";
		case Core::RepositoryOperation::Bisect:
			return "bisect in progress";
		case Core::RepositoryOperation::ApplyMailbox:
			return "applying patches";
		}

		return {};
	}
}
