#include "Git/GitStatusText.h"

#include "Core/Base/Strings.h"

#include "Git/Branches/BranchLookup.h"
#include "UI/TypographicMarks.h"

#include <IconsLucide.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {

		constexpr const char* kBranchGlyphPrefix = ICON_LC_GIT_BRANCH " ";

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
				return ICON_LC_CHECK;
			}

			std::string text;

			if (branch.Ahead > 0)
			{
				text = ICON_LC_ARROW_UP + std::to_string(branch.Ahead);
			}

			if (branch.Behind > 0)
			{
				if (!text.empty())
				{
					text += " ";
				}

				text += ICON_LC_ARROW_DOWN + std::to_string(branch.Behind);
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

		void CountChanges(GitStatusSegment& segment, const Core::WorkingTreeStatus* status)
		{
			if (status == nullptr || status->Entries.empty())
			{
				return;
			}

			const std::size_t changeCount = status->Entries.size();
			segment.Changes = std::string{kBullet} + " " + Core::Counted(changeCount, "change");
			segment.ChangesTooltip =
				changeCount == 1 ? "1 uncommitted change. Click to show it in Changes."
								 : std::to_string(changeCount) + " uncommitted changes. Click to show them in Changes.";
		}
	}

	std::string CheckingOutText(const std::string& targetLabel)
	{
		return "Checking out " + targetLabel + "…";
	}

	const char* RepositoryOperationName(Core::RepositoryOperation operation)
	{
		return OperationName(operation);
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
			segment.Activity = inputs.Activity;
			return segment;
		}

		if (inputs.CheckingOut.has_value())
		{
			GitStatusSegment segment;
			segment.HasRepository = true;
			segment.BranchBusy = true;
			segment.Branch = CheckingOutText(*inputs.CheckingOut);
			segment.BranchTooltip = "Checking out " + *inputs.CheckingOut + ". The branch is named here once it is done.";
			CountChanges(segment, inputs.Status);
			segment.Activity = inputs.Activity;
			return segment;
		}

		if (inputs.Head == nullptr)
		{
			if (!inputs.Activity.has_value())
			{
				return std::nullopt;
			}

			GitStatusSegment segment;
			segment.Activity = inputs.Activity;
			return segment;
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

		CountChanges(segment, inputs.Status);
		segment.Activity = inputs.Activity;

		return segment;
	}
}
