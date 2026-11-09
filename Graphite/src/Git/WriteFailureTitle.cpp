#include "Git/WriteFailureTitle.h"

#include <format>

namespace Graphite {

	std::string_view WriteFailureTitle(GitModel::WriteKind kind)
	{
		// WARNING: No default, so a kind added later is a warning here, and warnings fail the build.
		switch (kind)
		{
		case GitModel::WriteKind::Stage:
			return "Stage failed";
		case GitModel::WriteKind::Unstage:
			return "Unstage failed";
		case GitModel::WriteKind::Discard:
			return "Discard failed";
		case GitModel::WriteKind::Commit:
			return "Commit failed";
		case GitModel::WriteKind::CreateBranch:
			return "Create branch failed";
		case GitModel::WriteKind::RenameBranch:
			return "Rename branch failed";
		case GitModel::WriteKind::DeleteBranch:
			return "Delete branch failed";
		case GitModel::WriteKind::StashSave:
			return "Stash failed";
		case GitModel::WriteKind::StashApply:
			return "Stash apply failed";
		case GitModel::WriteKind::StashPop:
			return "Stash pop failed";
		case GitModel::WriteKind::StashDrop:
			return "Stash drop failed";
		case GitModel::WriteKind::Checkout:
			return "Checkout failed";
		case GitModel::WriteKind::FastForward:
			return "Pull failed";

		case GitModel::WriteKind::Init:
		case GitModel::WriteKind::DeleteCheck:
		case GitModel::WriteKind::CheckoutPlan:
			break;
		}

		return "Git action failed";
	}

	std::string WorktreeWriteFailureTitle(GitModel::WriteKind kind, std::string_view folder)
	{
		return std::format("{} in {}", WriteFailureTitle(kind), folder);
	}
}
