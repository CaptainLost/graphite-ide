#pragma once

#include "Git/Work/ChipRow.h"
#include "Git/Work/WorkBoard.h"
#include "Review/ReviewFiles.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// Chips in order: distance, uncommitted, twins, check, lock, detached, operation.
	std::vector<Chip> CardChips(const WorkCard& card, std::string_view trunkName);

	// The folder's own name, also for a path that ends in a separator, as libgit2 hands the main worktree's back.
	std::string WorktreeFolderName(const std::filesystem::path& worktree);

	// "feature · feature-folder": the short branch name and the worktree folder, either alone when the other is missing.
	std::string CardBranchLine(const WorkCard& card);

	// A row of the ready group: a branch ready to remove, or a worktree listed for information.
	struct ReadyLine
	{
		// The short branch name, else the worktree folder's.
		std::string Name;

		// The worktree folder beside a branch's name; empty without a worktree or when the name is the folder's.
		std::string Folder;

		// "merged into main", "same change already in main", "locked: …", "folder is gone", "detached, nothing to keep".
		std::string Reason;
		bool Locked = false;

		// Only a branch ready to remove has one.
		std::optional<std::chrono::sys_seconds> TipTime;

		friend bool operator==(const ReadyLine&, const ReadyLine&) = default;
	};

	// Row `index` of the ready group: the ready entries first, then the information rows, in the board's order. Built one
	// row at a time, for the rows on screen only.
	ReadyLine ReadyLineAt(const WorkBoard& board, std::size_t index);

	// The ready entries and the information rows together.
	std::size_t ReadyLineCount(const WorkBoard& board);

	// "33 worktrees · main ↑13 to push" over the cards; the count reads as waiting until the facts land, never as 0.
	std::string WorkSummaryText(const WorkBoard& board);

	// "There is no trunk to compare against." once the facts say the repository has neither origin's default branch
	// nor a branch checked out; empty while they are read and whenever there is a trunk.
	std::optional<std::string> NoTrunkNote(const WorkBoard& board);

	// "3 of 10 viewed" in the thread's header; "counting viewed files…" while the count is unknown, never a zero.
	std::string ReviewProgressText(const std::optional<ViewedCount>& viewed);

	// "3/10" at the end of a card's progress bar; "…" while the count is unknown, never a zero.
	std::string ReviewCountText(const std::optional<ViewedCount>& viewed);
}
