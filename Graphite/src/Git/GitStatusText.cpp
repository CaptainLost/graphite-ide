#include "Git/GitStatusText.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>

namespace Graphite {

	namespace {

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

		// The checked-out local branch, which is the only one whose distance the bar speaks
		// for. A remote branch of the same name is a different ref.
		const Core::Branch* CheckedOut(const std::vector<Core::Branch>* branches)
		{
			if (branches == nullptr)
				return nullptr;

			const auto found = std::find_if(branches->begin(), branches->end(),
											[](const Core::Branch& branch) {
												return branch.IsHead && branch.Kind == Core::RefKind::Local;
											});

			return found == branches->end() ? nullptr : &*found;
		}

		std::string Distance(const Core::Branch& branch)
		{
			if (!branch.Upstream.has_value() || !branch.DistanceKnown)
				return {};

			if (branch.Ahead == 0 && branch.Behind == 0)
				return ICON_MDI_CHECK;

			std::string text;

			if (branch.Ahead > 0)
				text = ICON_MDI_ARROW_UP + std::to_string(branch.Ahead);

			if (branch.Behind > 0)
			{
				if (!text.empty())
					text += " ";

				text += ICON_MDI_ARROW_DOWN + std::to_string(branch.Behind);
			}

			return text;
		}
	}

	std::optional<GitStatusSegment> GitStatusText(const GitStatusInputs& inputs)
	{
		if (!inputs.FolderOpen)
			return std::nullopt;

		if (inputs.MissingRepository)
		{
			GitStatusSegment segment;
			segment.Branch = "No repository";
			return segment;
		}

		if (inputs.Head == nullptr)
			return std::nullopt;

		const Core::HeadState& head = *inputs.Head;

		GitStatusSegment segment;
		segment.HasRepository = true;
		segment.Branch = ICON_MDI_SOURCE_BRANCH " ";

		switch (head.Kind)
		{
		case Core::HeadKind::Branch:
		{
			segment.Branch += head.BranchName;

			const Core::Branch* branch = CheckedOut(inputs.Branches);

			if (branch != nullptr && branch->Upstream.has_value())
			{
				segment.BranchTooltip = head.BranchName + " tracks " + *branch->Upstream;
				segment.Distance = Distance(*branch);
			}
			else
			{
				segment.BranchTooltip = head.BranchName + " has no upstream branch";
			}

			break;
		}

		case Core::HeadKind::Detached:
		{
			const std::string hash = head.Commit ? head.Commit->ShortHex() : std::string{};
			segment.Branch += hash + " (detached)";
			segment.BranchTooltip =
				"HEAD is detached at " + (head.Commit ? head.Commit->ToHex() : std::string{});
			break;
		}

		case Core::HeadKind::Unborn:
			segment.Branch += head.BranchName + " (no commits)";
			segment.BranchTooltip = "No commits yet on " + head.BranchName;
			break;
		}

		if (const char* operation = OperationName(head.Operation); operation != nullptr)
		{
			segment.Branch += " \xC2\xB7 ";
			segment.Branch += operation;
		}

		if (inputs.Status != nullptr && !inputs.Status->Entries.empty())
		{
			const std::size_t count = inputs.Status->Entries.size();
			segment.Changes = ICON_MDI_CIRCLE_MEDIUM " " + std::to_string(count) +
							  (count == 1 ? " change" : " changes");
		}

		return segment;
	}
}
