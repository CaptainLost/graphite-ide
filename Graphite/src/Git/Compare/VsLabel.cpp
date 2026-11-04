#include "Git/Compare/VsLabel.h"

#include "Core/Base/Count.h"

#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kPending = "\xE2\x80\xA6";
		constexpr const char* kWidestUsualBehind = "99,999";

		VsLabel Plain(std::string lead)
		{
			return VsLabel{.Lead = std::move(lead), .Line = {}, .Counts = {}, .Mark = std::nullopt};
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
			else
			{
				label.Lead = line.Kind == Core::ComparedKind::Branch ? "on top of" : "vs";
				label.Counts = Core::FormatCount(line.Ahead) + " ahead, " + BehindText(input.Distance) + " behind";
				label.CountsRoom = Core::FormatCount(line.Ahead) + " ahead, " + ReservedBehindText(input.Distance) + " behind";
			}

			return label;
		}
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

	std::string ReservedBehindText(const std::optional<Core::LineDistance>& distance)
	{
		const std::string usual = kWidestUsualBehind;
		const std::string landed = distance.has_value() ? Core::FormatCount(distance->Behind) : std::string{};

		return landed.size() > usual.size() ? landed : usual;
	}

	VsLabel DescribeVs(const VsInput& input)
	{
		if (input.Lines.BaseNamed(input.ShownRef) != nullptr)
		{
			return Plain("compare with\xE2\x80\xA6");
		}

		if (input.Lines.OldCopyNamed(input.ShownRef) != nullptr)
		{
			return Plain("compared with nothing");
		}

		const Core::BranchLine* const line = input.Lines.LineOf(input.ShownRef);

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
