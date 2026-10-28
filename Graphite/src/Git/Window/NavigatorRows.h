#pragma once

#include "Core/Git/Ref.h"
#include "Git/Window/GitWindowSelection.h"
#include "Git/Work/WorkBoard.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kNavigatorThreads = 6;
	inline constexpr std::size_t kNavigatorBranches = 6;

	enum class NavigatorRowKind
	{
		Work,
		Thread,
		ShowAllWork,
		Ready,
		Locked,
		Changes,
		BranchesHeading,
		Branch,
		ShowAllBranches
	};

	struct NavigatorRow
	{
		NavigatorRowKind Kind = NavigatorRowKind::Work;
		std::string Label;

		// At the row's right edge: a count, a thread's distance or a branch's age; "" while unknown.
		std::string Count;

		// Card index for Thread, branch index for Branch.
		std::size_t Index = 0;

		// Thread: the card's branch, empty for a detached worktree, which has none to review. Branch: its full name.
		std::string Branch;

		WorkState State = WorkState::Other;
		bool Selected = false;

		friend bool operator==(const NavigatorRow&, const NavigatorRow&) = default;
	};

	struct NavigatorInput
	{
		const WorkBoard* Board = nullptr;
		std::optional<std::size_t> ChangeCount;
		const std::vector<Core::Branch>* Branches = nullptr;
		GitWindowSelection Selection;

		// What a branch row's age is counted to.
		std::chrono::sys_seconds Now{};
	};

	std::vector<NavigatorRow> BuildNavigatorRows(const NavigatorInput& input);

	// Local branches only: HEAD's first, then the newest tip; a branch whose tip cannot be read comes last.
	std::vector<std::size_t> NewestLocalBranches(const std::vector<Core::Branch>& branches, std::size_t count);

	// The worktree folder's name, else the short branch name.
	std::string ThreadRowLabel(const WorkCard& card);

	// "↑4 ↓38" against the trunk: each side only when it is not zero, nothing while the distance is unknown.
	std::string ThreadDistanceText(const WorkCard& card);

	// What a click on the row selects.
	GitWindowSelection SelectionOf(const NavigatorRow& row);
}
