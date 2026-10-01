#pragma once

#include "Core/Git/Checkout.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Status.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct CheckoutQuestionRow
	{
		std::filesystem::path Path;

		// The editor holds changes to this file that are not on disk.
		bool Unsaved = false;

		// The checkout would overwrite this file, so it cannot go ahead as it is.
		bool Blocks = false;
	};

	struct CheckoutQuestion
	{
		// "Checking out feature/x would overwrite your changes"
		std::string Title;

		// Tracked changes and unsaved buffers, sorted, unique.
		std::vector<CheckoutQuestionRow> Changed;

		std::vector<CheckoutQuestionRow> Untracked;

		// An untracked file blocks: the checkbox is ticked and locked.
		bool UntrackedForced = false;
	};

	// What makes a row matter, as the dialog says it beside the path: "unsaved", "blocks
	// checkout", both, or nothing.
	std::string CheckoutRowTag(const CheckoutQuestionRow& row);

	// All paths are relative to the working directory. Matching between them ignores
	// separator style and ASCII case, because the editor and git spell the same file
	// differently on Windows.
	//
	// unsaved: relative paths of documents modified in the editor. Git cannot see those, so
	// a checkout that rewrites one has to ask even when it overwrites nothing of git's.
	bool NeedsCheckoutDialog(const Core::CheckoutPlan& plan, const std::vector<std::filesystem::path>& unsaved);

	CheckoutQuestion BuildCheckoutQuestion(const std::string& targetLabel, const Core::CheckoutPlan& plan,
	                                       const Core::WorkingTreeStatus& status,
	                                       const std::vector<std::filesystem::path>& unsaved);

	// The paths a finished checkout changed that the editor may reload without asking: every
	// one but a document with unsaved edits now. A document the dialog reverted or saved is
	// clean by then unless it was typed into again, and one never asked about was edited
	// after the plan; either way the editor's own "changed on disk" question asks about it
	// instead of a reload throwing the edits away. All paths absolute; matched without
	// regard to case or separators.
	std::vector<std::filesystem::path> SilentReloads(const std::vector<std::filesystem::path>& changed,
	                                                 const std::vector<std::filesystem::path>& unsavedNow);

	// nullopt when the name is fine; otherwise the reason to show under the field.
	std::optional<std::string> BranchNameProblem(const std::string& name, const std::vector<Core::Branch>& branches);

	// "feature/x", "origin/x" or a short commit id.
	std::string CheckoutTargetLabel(const Core::CheckoutTarget& target);
}
