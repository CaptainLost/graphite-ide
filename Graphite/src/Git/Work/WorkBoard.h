#pragma once

#include "Core/Git/Head.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"
#include "Git/Work/WorkPort.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	enum class WorkCardKind
	{
		WorkingCopy,
		Branch,
		DetachedWorktree
	};

	// The navigator dot: green, amber, grey.
	enum class WorkState
	{
		CanFastForward,
		BehindTrunk,
		Other
	};

	enum class ReadyReason
	{
		Merged,
		SameChangeInTrunk
	};

	enum class WorkInfoKind
	{
		Locked,
		FolderMissing,
		DetachedClean
	};

	struct WorkCard
	{
		WorkCardKind Kind = WorkCardKind::Branch;

		// Full name; HEAD's for the working copy ("HEAD" when detached); empty for a detached worktree.
		std::string Branch;
		std::optional<std::filesystem::path> Worktree;
		std::string Title;

		// The title is a Claude session's, not the branch's or folder's name.
		bool FromSession = false;
		std::string TipSubject;
		std::optional<std::chrono::sys_seconds> TipTime;

		// Against the trunk; the working copy's against its upstream.
		std::size_t Ahead = 0;
		std::size_t Behind = 0;
		bool DistanceKnown = false;
		std::optional<std::size_t> Uncommitted;
		std::size_t OwnChanges = 0;
		std::size_t InTrunk = 0;

		// The patch check answered and was complete.
		bool Checked = false;
		bool Locked = false;
		Core::RepositoryOperation Operation = Core::RepositoryOperation::None;
		std::optional<Core::Oid> DetachedAt;
		WorkState State = WorkState::Other;
	};

	struct ReadyEntry
	{
		std::string Branch;
		std::optional<std::filesystem::path> Worktree;
		ReadyReason Reason = ReadyReason::Merged;
		std::optional<std::chrono::sys_seconds> TipTime;
	};

	struct WorkInfo
	{
		WorkInfoKind Kind = WorkInfoKind::Locked;
		std::filesystem::path Worktree;
		std::string Branch;
		std::string Detail;
	};

	struct WorkBoard
	{
		// Full name.
		std::optional<std::string> Trunk;

		// The working copy first.
		std::vector<WorkCard> Cards;

		// Newest tip first.
		std::vector<ReadyEntry> Ready;
		std::vector<WorkInfo> Info;

		// No facts answer the current branches yet.
		bool FactsPending = false;

		// Every worktree of the repository, the main one too; unknown until the first facts land.
		std::optional<std::size_t> Worktrees;
	};

	struct WorkBoardInput
	{
		const std::vector<Core::Branch>* Branches = nullptr;

		// Null until a first answer lands.
		const WorkFacts* Facts = nullptr;

		// GitModel's status entry count.
		std::optional<std::size_t> WorkingCopyChanges;
		std::function<std::optional<std::string>(const std::filesystem::path& worktree)> TitleOf;
	};

	// WARNING: Facts are current when they answer WorkFactsRequestFor(branches, their own budget); the
	// board has no budget of its own to compare.
	WorkBoard BuildWorkBoard(const WorkBoardInput& input);

	// The trunk and every local branch but the trunk and HEAD's. With no trunk, no branches and a zero tip.
	WorkFactsRequest WorkFactsRequestFor(const std::vector<Core::Branch>& branches, std::size_t patchBudget);

	// "27 merged into main, 3 with the same change already in main · 1 locked by another session"; the
	// locked part only when some are locked.
	std::string ReadySummary(const WorkBoard& board);
}
