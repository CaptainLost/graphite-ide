#include "Git/History/ShownHistory.h"

#include "Git/Content/ContentOrder.h"
#include "Git/Tags/TagLookup.h"
#include "Git/Worktrees/WorktreeListing.h"
#include "Git/Worktrees/WorktreesModel.h"

#include "Core/Git/Ref.h"

#include <cstddef>

namespace Graphite {

	ShownHistory::ShownHistory(const ShownHistoryInput& input)
		: m_Input(input)
	{
	}

	bool ShownHistory::ShowsTag() const
	{
		return m_Input.SelectedRef.has_value() && IsTagName(*m_Input.SelectedRef);
	}

	bool ShownHistory::IsHeadHistory() const
	{
		if (m_Input.RestoreWaits || m_Input.SelectedStash.has_value() || WorktreeShown() != nullptr)
		{
			return false;
		}

		if (!m_Input.SelectedRef.has_value())
		{
			return true;
		}

		const std::string_view checkedOut = m_Input.OnScreen.CheckedOutName();

		return !checkedOut.empty() && checkedOut == *m_Input.SelectedRef;
	}

	HeadInRead ShownHistory::HeadPlacement() const
	{
		return PlaceHeadInRead(HeadInReadInput{.Head = m_Input.Head,
											   .ReadCheckedOut = m_Input.OnScreen.CheckedOutName(),
											   .ReadComing = m_Input.Reading || m_Input.ListingComing,
											   .ReadFailed = m_Input.ReadFailed});
	}

	std::string ShownHistory::RefName() const
	{
		// A tag's history is its commit's, combined: no fold row, no vs button, no banner.
		// A detached worktree's HEAD, like the open folder's, has no line to compare it with.
		if (m_Input.SelectedStash.has_value() || m_Input.RestoreWaits || ShowsTag() || WorktreeShown() != nullptr)
		{
			return {};
		}

		if (m_Input.SelectedRef.has_value())
		{
			return *m_Input.SelectedRef;
		}

		const std::string_view checkedOut = m_Input.OnScreen.CheckedOutName();

		if (checkedOut.empty())
		{
			return {};
		}

		// WARNING: After a checkout the read names the branch checked out before until the next read lands; its line,
		// banner and fold row would describe that branch under HEAD's new name.
		if (HeadPlacement() != HeadInRead::Agrees)
		{
			return {};
		}

		return std::string{checkedOut};
	}

	HistoryLine ShownHistory::RefLine() const
	{
		const BranchLines* const lines = m_Input.OnScreen.GetLines();
		const std::string shown = RefName();

		if (lines == nullptr || shown.empty())
		{
			return {};
		}

		return HistoryLineOf(*m_Input.OnScreen.GetRead(), *lines, shown, m_Input.NewerShown.contains(shown));
	}

	HistoryLine ShownHistory::LineToShow() const
	{
		return m_Input.FileHistory ? HistoryLine{} : RefLine();
	}

	bool ShownHistory::WaitsForLines() const
	{
		// A tag's and a detached worktree's history are read from their commit alone: the read has no line for them to
		// wait for.
		if (m_Input.SelectedStash.has_value() || m_Input.FileHistory || ShowsTag() || WorktreeShown() != nullptr)
		{
			return false;
		}

		const BranchLines* const lines = m_Input.OnScreen.GetLines();

		return HistoryWaitsForLines(HistoryWaitInput{
			.HasRead = lines != nullptr,
			.Reading = m_Input.Reading,
			.ReadFailed = m_Input.ReadFailed,
			.BranchesFailed = m_Input.BranchesFailed,
			.ReadLacksShown = lines != nullptr && m_Input.SelectedRef.has_value() && !lines->IndexOf(*m_Input.SelectedRef).has_value(),
			.Head = m_Input.SelectedRef.has_value() ? HeadInRead::Agrees : HeadPlacement()});
	}

	bool ShownHistory::ShowsLocalChanges() const
	{
		// A file's history is about that file, not about the whole tree's changes; a history still being decided is none.
		if (m_Input.SelectedStash.has_value() || m_Input.FileHistory || m_Input.RestoreWaits || WorktreeShown() != nullptr)
		{
			return false;
		}

		if (!m_Input.SelectedRef.has_value())
		{
			return true;
		}

		const Core::HeadState* const head = m_Input.Head;

		return head != nullptr && head->Kind == Core::HeadKind::Branch && Core::LocalBranchReference(head->BranchName) == *m_Input.SelectedRef;
	}

	const Core::WorktreeEntry* ShownHistory::WorktreeShown() const
	{
		// A stash selected later, or a ref, is what the history shows; the owner lets go of the worktree then anyway.
		if (!m_Input.SelectedWorktree.has_value() || m_Input.Worktrees == nullptr || m_Input.SelectedStash.has_value() ||
			m_Input.SelectedRef.has_value())
		{
			return nullptr;
		}

		const std::vector<Core::WorktreeEntry>* const entries = m_Input.Worktrees->Entries();

		if (entries == nullptr)
		{
			return nullptr;
		}

		const std::optional<std::size_t> index = IndexOfWorktree(*entries, *m_Input.SelectedWorktree);

		if (!index.has_value())
		{
			return nullptr;
		}

		// WARNING: A missing folder's HEAD is not read, and one that moved onto a branch has that branch's history: either
		// way there is no commit of the worktree's own to walk from.
		const Core::WorktreeEntry& entry = (*entries)[*index];

		if (entry.FolderMissing || entry.Head.Kind != Core::HeadKind::Detached || !entry.Head.Commit.has_value())
		{
			return nullptr;
		}

		return &entry;
	}

	std::optional<std::filesystem::path> ShownHistory::ChangesWorktree() const
	{
		// As for Local changes: a file's history is about that file, and a history still being decided shows no row.
		if (m_Input.SelectedStash.has_value() || m_Input.FileHistory || m_Input.RestoreWaits)
		{
			return std::nullopt;
		}

		if (const Core::WorktreeEntry* const shown = WorktreeShown())
		{
			return shown->Path;
		}

		// The open folder's HEAD and its branch are never another worktree's, and a missing folder is no branch's.
		if (!m_Input.SelectedRef.has_value() || m_Input.Worktrees == nullptr)
		{
			return std::nullopt;
		}

		return m_Input.Worktrees->OtherWorktreeOf(*m_Input.SelectedRef);
	}

	std::optional<DistanceTips> ShownHistory::DistanceTipsOf(std::string_view shownRef) const
	{
		const BranchLines* const lines = m_Input.OnScreen.GetLines();

		if (lines == nullptr)
		{
			return std::nullopt;
		}

		const LineRead& read = *m_Input.OnScreen.GetRead();
		const ShownComparison comparison = lines->ComparisonOf(shownRef);

		switch (comparison.What)
		{
		case ShownComparison::Kind::Pick:
		{
			// A pick for a base or an old copy is measured from the reference whose history is shown, as its fold row is.
			const std::optional<std::size_t> self = lines->IndexOf(shownRef);

			return self.has_value() ? std::optional<DistanceTips>{DistanceTips{read.Refs[*self].Tip, read.Refs[*comparison.Picked].Tip}}
									: std::nullopt;
		}
		case ShownComparison::Kind::OldCopy:
			return DistanceTips{read.Refs[comparison.OldCopy->Ref].Tip, read.Refs[comparison.OldCopy->Of].Tip};
		case ShownComparison::Kind::Line:
		{
			const Core::BranchLine& line = *comparison.Line;

			// Behind is said only for a branch that is neither merged nor without commits of its own.
			if (!line.ComparedWith.has_value() || line.State != Core::BranchState::NotMerged)
			{
				return std::nullopt;
			}

			return DistanceTips{read.Refs[line.Ref].Tip, read.Refs[*line.ComparedWith].Tip};
		}
		case ShownComparison::Kind::Base:
		case ShownComparison::Kind::Nothing:
			return std::nullopt;
		}

		return std::nullopt;
	}

	const Core::BranchLine* ShownHistory::ContentCandidate() const
	{
		const BranchLines* const lines = m_Input.OnScreen.GetLines();
		const std::string shownRef = RefName();

		if (lines == nullptr || shownRef.empty())
		{
			return nullptr;
		}

		// The line DescribeVs describes as a branch's; a pick or an old copy has no state of the model's to check.
		const ShownComparison comparison = lines->ComparisonOf(shownRef);

		if (comparison.What != ShownComparison::Kind::Line || !IsContentCandidate(*comparison.Line))
		{
			return nullptr;
		}

		return comparison.Line;
	}
}
