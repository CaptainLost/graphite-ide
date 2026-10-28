#pragma once

#include "Git/Branches/BranchAuthors.h"
#include "Git/Work/WorkPort.h"

#include "Core/Git/PatchMatch.h"
#include "Core/Git/Ref.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	enum class BranchScope
	{
		All,
		Local,
		RemoteOnly,
		Mine,
		Stale
	};

	enum class BranchColumn
	{
		Name,
		Where,
		Trunk,
		Age,
		Authors,
		Subject
	};

	inline constexpr std::chrono::days kStaleAfter{90};

	struct BranchTableInput
	{
		const std::vector<Core::Branch>* Branches = nullptr;
		const BranchAuthors* Authors = nullptr;
		const WorkFacts* Facts = nullptr;
		std::set<std::string> HeldByWorktree; // local full names checked out in a linked worktree
		BranchScope Scope = BranchScope::All;
		BranchColumn SortBy = BranchColumn::Age;
		bool Ascending = false; // Age: newest first
		std::string Filter;
		std::chrono::sys_seconds Now;

		friend bool operator==(const BranchTableInput&, const BranchTableInput&) = default;
	};

	struct BranchTableRow
	{
		std::string Name;
		std::optional<std::size_t> Local; // index into Branches
		std::optional<std::size_t> Remote;
		std::string RemoteName; // "origin" for "origin/feat"; empty with no Remote
		bool InWorktree = false;
		bool IsHead = false;
		std::optional<Core::PatchMatch> Trunk;
		std::optional<std::chrono::sys_seconds> TipTime; // the newer of the two tips
		std::vector<BranchAuthor> Authors;
		std::string Subject;
	};

	std::vector<BranchTableRow> BuildBranchTable(const BranchTableInput& input);

	// Rows per scope, in BranchScope order, with the filter applied and the scope ignored.
	std::array<std::size_t, 5> CountBranchScopes(const BranchTableInput& input);

	// "local", "origin", "local · origin", then " · worktree" when held.
	std::string WhereText(const BranchTableRow& row);
}
