#include "Git/GitStatusText.h"

#include "Core/Base/Strings.h"

#include "Git/DefaultBranch.h"
#include "UI/TypographicMarks.h"

#include <IconsMaterialDesignIcons.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kBranchGlyphPrefix = ICON_MDI_SOURCE_BRANCH " ";

		const char* OperationName(Core::RepositoryOperation operation)
		{
			switch (operation)
			{
			case Core::RepositoryOperation::Merge:
				return "MERGING";
			case Core::RepositoryOperation::Rebase:
				return "REBASING";
			case Core::RepositoryOperation::CherryPick:
				return "CHERRY-PICKING";
			case Core::RepositoryOperation::Revert:
				return "REVERTING";
			case Core::RepositoryOperation::Bisect:
				return "BISECTING";
			case Core::RepositoryOperation::ApplyMailbox:
				return "APPLYING";
			case Core::RepositoryOperation::None:
				break;
			}

			return nullptr;
		}



		std::string DistanceMarks(const Core::Branch& branch)
		{
			if (!branch.Upstream.has_value() || !branch.DistanceKnown)
			{
				return {};
			}

			if (branch.Ahead == 0 && branch.Behind == 0)
			{
				return ICON_MDI_CHECK;
			}

			std::string text;

			if (branch.Ahead > 0)
			{
				text = ICON_MDI_ARROW_UP + std::to_string(branch.Ahead);
			}

			if (branch.Behind > 0)
			{
				if (!text.empty())
				{
					text += " ";
				}

				text += ICON_MDI_ARROW_DOWN + std::to_string(branch.Behind);
			}

			return text;
		}

		std::string CommitCountLabel(std::size_t commitCount)
		{
			return Core::Counted(commitCount, "commit");
		}

		std::string DistanceSentence(const Core::Branch& branch)
		{
			if (!branch.Upstream.has_value() || !branch.DistanceKnown)
			{
				return {};
			}

			const std::string& upstream = *branch.Upstream;

			if (branch.Ahead == 0 && branch.Behind == 0)
			{
				return "Up to date with " + upstream;
			}

			if (branch.Behind == 0)
			{
				return CommitCountLabel(branch.Ahead) + " ahead of " + upstream;
			}

			if (branch.Ahead == 0)
			{
				return CommitCountLabel(branch.Behind) + " behind " + upstream;
			}

			return CommitCountLabel(branch.Ahead) + " ahead of " + upstream + " and " +
				   CommitCountLabel(branch.Behind) + " behind it";
		}
	}

	std::optional<GitStatusSegment> GitStatusText(const GitStatusInputs& inputs)
	{
		if (!inputs.FolderOpen)
		{
			return std::nullopt;
		}

		if (inputs.MissingRepository)
		{
			GitStatusSegment segment;
			segment.Branch = "No repository";
			return segment;
		}

		if (inputs.Head == nullptr)
		{
			return std::nullopt;
		}

		const Core::HeadState& head = *inputs.Head;

		GitStatusSegment segment;
		segment.HasRepository = true;
		segment.Branch = kBranchGlyphPrefix;

		switch (head.Kind)
		{
		case Core::HeadKind::Branch:
		{
			segment.Branch += head.BranchName;

			const Core::Branch* branch = inputs.Branches != nullptr ? CheckedOutBranch(*inputs.Branches) : nullptr;

			if (branch != nullptr && branch->Upstream.has_value())
			{
				segment.BranchTooltip = head.BranchName + " tracks " + *branch->Upstream;
				segment.Distance = DistanceMarks(*branch);
				segment.DistanceTooltip = DistanceSentence(*branch);
			}
			else
			{
				segment.BranchTooltip = head.BranchName + " has no upstream branch";
			}

			break;
		}

		case Core::HeadKind::Detached:
		{
			const std::string shortHash = head.Commit ? head.Commit->ShortHex() : std::string{};
			const std::string fullHash = head.Commit ? head.Commit->ToHex() : std::string{};
			segment.Branch += shortHash + " (detached)";
			segment.BranchTooltip = "HEAD is detached at " + fullHash;
			break;
		}

		case Core::HeadKind::Unborn:
			segment.Branch += head.BranchName + " (no commits)";
			segment.BranchTooltip = "No commits yet on " + head.BranchName;
			break;
		}

		if (const char* operationName = OperationName(head.Operation); operationName != nullptr)
		{
			segment.Branch += kMiddleDotSeparator;
			segment.Branch += operationName;
		}

		if (inputs.Status != nullptr && !inputs.Status->Entries.empty())
		{
			const std::size_t changeCount = inputs.Status->Entries.size();
			segment.Changes = ICON_MDI_CIRCLE_MEDIUM " " + Core::Counted(changeCount, "change");
			segment.ChangesTooltip =
				changeCount == 1 ? "1 uncommitted change. Click to show it in Changes."
								 : std::to_string(changeCount) + " uncommitted changes. Click to show them in Changes.";
		}

		return segment;
	}

	std::optional<std::string> GitBranchLabel(const GitStatusInputs& inputs)
	{
		const std::optional<GitStatusSegment> segment = GitStatusText(inputs);

		if (!segment.has_value() || !segment->HasRepository)
		{
			return std::nullopt;
		}

		return segment->Branch.substr(std::string_view{kBranchGlyphPrefix}.size());
	}
}
