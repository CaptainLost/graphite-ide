#pragma once

#include "Git/Work/WorkBoard.h"
#include "Review/ReviewFiles.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// Chip texts in order: distance, uncommitted, twins, check, lock, detached, operation.
	std::vector<std::string> CardChips(const WorkCard& card, std::string_view trunkName);

	// The folder's own name, also for a path that ends in a separator, as libgit2 hands the main worktree's back.
	std::string WorktreeFolderName(const std::filesystem::path& worktree);

	// "feature · feature-folder": the short branch name and the worktree folder, either alone when the other is missing.
	std::string CardBranchLine(const WorkCard& card);

	// "33 worktrees · main ↑13 to push" over the cards; the count reads as waiting until the facts land, never as 0.
	std::string WorkSummaryText(const WorkBoard& board);

	// "Reviewed 3/10"; "Reviewed …" while the count is unknown, never a zero.
	std::string ReviewProgressText(const std::optional<ViewedCount>& viewed);
}
