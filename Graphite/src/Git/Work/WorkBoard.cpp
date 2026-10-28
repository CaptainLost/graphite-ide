#include "Git/Work/WorkBoard.h"

#include "Core/FileSystem/FileSystem.h"
#include "Git/DefaultBranch.h"

#include <algorithm>

namespace Graphite {

	namespace {

		// What the working copy's card names while HEAD is on no branch.
		constexpr const char* kDetachedHead = "HEAD";

		bool IsWorkBranch(const Core::Branch& branch, const Core::Branch* trunk)
		{
			return branch.Kind == Core::RefKind::Local && !branch.IsHead && &branch != trunk;
		}

		const WorktreeFacts* WorkingCopyWorktree(const WorkFacts* facts, const std::filesystem::path& workingDirectory)
		{
			if (facts == nullptr)
			{
				return nullptr;
			}

			if (!workingDirectory.empty())
			{
				const Core::PathKeyText key = Core::PathKey(workingDirectory);

				for (const WorktreeFacts& worktree : facts->Worktrees)
				{
					if (Core::PathKey(worktree.Entry.Path) == key)
					{
						return &worktree;
					}
				}
			}

			for (const WorktreeFacts& worktree : facts->Worktrees)
			{
				if (worktree.Entry.IsMain)
				{
					return &worktree;
				}
			}

			return nullptr;
		}

		const WorktreeFacts* WorktreeHolding(const WorkFacts* facts, const std::string& branchFullName)
		{
			if (facts == nullptr)
			{
				return nullptr;
			}

			for (const WorktreeFacts& worktree : facts->Worktrees)
			{
				const Core::WorktreeEntry& entry = worktree.Entry;

				// The main worktree counts too: open in a linked worktree, it holds a branch that is not HEAD's.
				if (!entry.FolderMissing && entry.Head.Kind == Core::HeadKind::Branch &&
					Core::LocalBranchReference(entry.Head.BranchName) == branchFullName)
				{
					return &worktree;
				}
			}

			return nullptr;
		}

		const BranchFacts* FactsOf(const WorkFacts* facts, const std::string& branchFullName)
		{
			if (facts == nullptr)
			{
				return nullptr;
			}

			for (const BranchFacts& branch : facts->Branches)
			{
				if (branch.FullName == branchFullName)
				{
					return &branch;
				}
			}

			return nullptr;
		}

		bool TipMovedSince(const WorkFacts* facts, const Core::Branch& branch)
		{
			for (const BranchTip& answered : facts->Answers.Branches)
			{
				if (answered.FullName == branch.FullName)
				{
					return answered.Tip != branch.Tip;
				}
			}

			return false;
		}

		WorkState StateOf(std::size_t ahead, std::size_t behind)
		{
			if (behind > 0)
			{
				return WorkState::BehindTrunk;
			}

			return ahead > 0 ? WorkState::CanFastForward : WorkState::Other;
		}

		std::optional<std::string> TitleFor(const WorkBoardInput& input, const std::filesystem::path& worktree)
		{
			return input.TitleOf ? input.TitleOf(worktree) : std::nullopt;
		}

		void SetTip(WorkCard& card, const Core::Branch& branch)
		{
			if (branch.TipCommit.has_value())
			{
				card.TipSubject = branch.TipCommit->Summary;
				card.TipTime = branch.TipCommit->Committer.When;
			}
		}

		WorkCard WorkingCopyCard(const WorkBoardInput& input, const Core::Branch* head, const WorktreeFacts* own)
		{
			WorkCard card;
			card.Kind = WorkCardKind::WorkingCopy;
			card.Branch = head != nullptr ? head->FullName : kDetachedHead;
			card.Uncommitted = input.WorkingCopyChanges;

			if (own != nullptr)
			{
				card.Worktree = own->Entry.Path;
				card.Operation = own->Entry.Head.Operation;
			}

			std::optional<std::string> title;

			if (own != nullptr)
			{
				title = TitleFor(input, own->Entry.Path);
			}

			card.Title = title.value_or(head != nullptr ? head->Name : kDetachedHead);
			card.FromSession = title.has_value();

			if (head != nullptr)
			{
				SetTip(card, *head);
				card.Ahead = head->Ahead;
				card.Behind = head->Behind;
				card.DistanceKnown = head->DistanceKnown;
			}

			return card;
		}

		bool NewestFirst(const std::optional<std::chrono::sys_seconds>& left, const std::optional<std::chrono::sys_seconds>& right)
		{
			if (!left.has_value() || !right.has_value())
			{
				return left.has_value() && !right.has_value();
			}

			return *left > *right;
		}
	}

	WorkFactsRequest WorkFactsRequestFor(const std::vector<Core::Branch>& branches, std::size_t patchBudget)
	{
		WorkFactsRequest request;
		request.PatchBudget = patchBudget;
		const Core::Branch* trunk = TrunkBranch(branches);

		if (trunk == nullptr)
		{
			return request;
		}

		request.TrunkFullName = trunk->FullName;
		request.TrunkTip = trunk->Tip;

		for (const Core::Branch& branch : branches)
		{
			if (IsWorkBranch(branch, trunk))
			{
				request.Branches.push_back({.FullName = branch.FullName, .Tip = branch.Tip});
			}
		}

		return request;
	}

	WorkBoard BuildWorkBoard(const WorkBoardInput& input)
	{
		static const std::vector<Core::Branch> kNoBranches;
		const std::vector<Core::Branch>& branches = input.Branches != nullptr ? *input.Branches : kNoBranches;
		const WorkFacts* facts = input.Facts;
		const Core::Branch* trunk = TrunkBranch(branches);
		const Core::Branch* head = CheckedOutBranch(branches);

		WorkBoard board;

		if (trunk != nullptr)
		{
			board.Trunk = trunk->FullName;
		}

		board.FactsPending = facts == nullptr || facts->Answers != WorkFactsRequestFor(branches, facts->Answers.PatchBudget);
		const WorktreeFacts* const own = WorkingCopyWorktree(facts, input.WorkingDirectory);
		board.Cards.push_back(WorkingCopyCard(input, head, own));

		if (facts != nullptr)
		{
			board.Worktrees = facts->Worktrees.size();
		}

		if (trunk == nullptr)
		{
			return board;
		}

		std::vector<WorkCard> branchCards;

		for (const Core::Branch& branch : branches)
		{
			if (!IsWorkBranch(branch, trunk))
			{
				continue;
			}

			const BranchFacts* branchFacts = FactsOf(facts, branch.FullName);

				// WARNING: Facts read at another tip say nothing about this one; the branch counts as unchecked.
				if (branchFacts != nullptr && TipMovedSince(facts, branch))
				{
					branchFacts = nullptr;
				}
			const WorktreeFacts* worktree = WorktreeHolding(facts, branch.FullName);
			const Core::PatchMatch* match = branchFacts != nullptr && branchFacts->Match.has_value() ? &*branchFacts->Match : nullptr;

			const bool unchecked = match == nullptr || !match->Complete;
			const bool hasChangesOfItsOwn = match != nullptr && match->OwnChanges > match->InBase;
			const bool worktreeDirty = worktree != nullptr && (!worktree->Uncommitted.has_value() || *worktree->Uncommitted > 0);

			if (!unchecked && !hasChangesOfItsOwn && !worktreeDirty)
			{
				const bool merged = match->Ahead == 0 || match->OwnChanges == 0;

				if (worktree != nullptr && worktree->Entry.Locked)
				{
					board.Info.push_back({.Kind = WorkInfoKind::Locked,
						.Worktree = worktree->Entry.Path,
						.Branch = branch.FullName,
						.Detail = worktree->Entry.LockReason});
					continue;
				}

				ReadyEntry entry;
				entry.Branch = branch.FullName;
				entry.Reason = merged ? ReadyReason::Merged : ReadyReason::SameChangeInTrunk;

				if (worktree != nullptr)
				{
					entry.Worktree = worktree->Entry.Path;
				}

				if (branch.TipCommit.has_value())
				{
					entry.TipTime = branch.TipCommit->Committer.When;
				}

				board.Ready.push_back(std::move(entry));
				continue;
			}

			WorkCard card;
			card.Branch = branch.FullName;
			SetTip(card, branch);
			std::optional<std::string> title;

			if (worktree != nullptr)
			{
				card.Worktree = worktree->Entry.Path;
				card.Uncommitted = worktree->Uncommitted;
				card.Locked = worktree->Entry.Locked;
				card.Operation = worktree->Entry.Head.Operation;
				title = TitleFor(input, worktree->Entry.Path);
			}

			card.Title = title.value_or(branch.Name);
			card.FromSession = title.has_value();

			if (match != nullptr)
			{
				card.Ahead = match->Ahead;
				card.Behind = match->Behind;
				card.DistanceKnown = true;
				card.OwnChanges = match->OwnChanges;
				card.InTrunk = match->InBase;
				card.Checked = match->Complete;
				card.State = StateOf(card.Ahead, card.Behind);
			}

			branchCards.push_back(std::move(card));
		}

		std::stable_sort(branchCards.begin(), branchCards.end(),
			[](const WorkCard& left, const WorkCard& right) { return NewestFirst(left.TipTime, right.TipTime); });
		std::stable_sort(board.Ready.begin(), board.Ready.end(),
			[](const ReadyEntry& left, const ReadyEntry& right) { return NewestFirst(left.TipTime, right.TipTime); });

		std::vector<WorkCard> detachedCards;

		if (facts != nullptr)
		{
			for (const WorktreeFacts& worktree : facts->Worktrees)
			{
				const Core::WorktreeEntry& entry = worktree.Entry;

				// The open folder's own worktree is the working copy's card.
				if (&worktree == own)
				{
					continue;
				}

				if (entry.FolderMissing)
				{
					board.Info.push_back({.Kind = WorkInfoKind::FolderMissing, .Worktree = entry.Path});
					continue;
				}

				if (entry.Head.Kind != Core::HeadKind::Detached)
				{
					continue;
				}

				const bool hasWork = !worktree.Uncommitted.has_value() || *worktree.Uncommitted > 0 ||
					entry.Head.Operation != Core::RepositoryOperation::None;

				if (!hasWork)
				{
					board.Info.push_back({.Kind = WorkInfoKind::DetachedClean, .Worktree = entry.Path});
					continue;
				}

				WorkCard card;
				card.Kind = WorkCardKind::DetachedWorktree;
				card.Worktree = entry.Path;
				const std::optional<std::string> sessionTitle = TitleFor(input, entry.Path);
				card.Title = sessionTitle.value_or(Core::ToUtf8(entry.Path.filename()));
				card.FromSession = sessionTitle.has_value();
				card.Uncommitted = worktree.Uncommitted;
				card.Locked = entry.Locked;
				card.Operation = entry.Head.Operation;
				card.DetachedAt = entry.Head.Commit;
				detachedCards.push_back(std::move(card));
			}
		}

		for (WorkCard& card : branchCards)
		{
			board.Cards.push_back(std::move(card));
		}

		for (WorkCard& card : detachedCards)
		{
			board.Cards.push_back(std::move(card));
		}

		return board;
	}

	bool CardNamesBranch(const WorkCard& card)
	{
		return card.Kind != WorkCardKind::DetachedWorktree && !card.Branch.empty() && card.Branch != kDetachedHead;
	}

	std::string ReadySummary(const WorkBoard& board)
	{
		std::size_t merged = 0;
		std::size_t sameChange = 0;
		std::size_t locked = 0;

		for (const ReadyEntry& entry : board.Ready)
		{
			(entry.Reason == ReadyReason::Merged ? merged : sameChange)++;
		}

		for (const WorkInfo& info : board.Info)
		{
			locked += info.Kind == WorkInfoKind::Locked ? 1 : 0;
		}

		const std::string trunk = board.Trunk.has_value() ? Core::ShortBranchName(*board.Trunk) : "the trunk";
		std::string summary = std::to_string(merged) + " merged into " + trunk + ", " + std::to_string(sameChange) +
			" with the same change already in " + trunk;

		if (locked > 0)
		{
			summary += " · " + std::to_string(locked) + " locked by another session";
		}

		return summary;
	}
}
