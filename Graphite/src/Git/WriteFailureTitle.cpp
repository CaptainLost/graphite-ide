#include "Git/WriteFailureTitle.h"

namespace Graphite {

	std::string_view WriteFailureTitle(GitModel::WriteKind kind)
	{
		// No default: a kind added later is a warning here, and warnings fail the build.
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

		// These report their failures elsewhere - init in its prompt, the delete check and
		// the checkout plan to the dialog waiting on them - and never reach the queue; named
		// all the same, should one ever arrive.
		case GitModel::WriteKind::Init:
		case GitModel::WriteKind::DeleteCheck:
		case GitModel::WriteKind::CheckoutPlan:
			break;
		}

		return "Git action failed";
	}
}
