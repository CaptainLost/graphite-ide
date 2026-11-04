#include "Git/History/CommitRows.h"

#include "Git/Changes/ChangesRows.h"

#include "Core/Base/Count.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr std::size_t kPlaceholderFileRowCount = 1;

		CommitRow RowOfKind(CommitRowKind kind, std::size_t commit = 0)
		{
			CommitRow row;
			row.Kind = kind;
			row.Commit = commit;
			return row;
		}
	}

	std::size_t FileRowsOfExpanded(std::size_t fileCount)
	{
		return fileCount == 0 ? kPlaceholderFileRowCount : fileCount;
	}

	bool IsMerge(const Core::CommitSummary& commit)
	{
		return commit.Parents.size() > 1;
	}

	std::size_t BroughtRowsOf(const Core::LogPage* brought)
	{
		constexpr std::size_t kStandInRow = 1;

		return brought == nullptr ? kStandInRow : std::max(brought->Commits.size(), kStandInRow);
	}

	std::string DescribeBrought(const Core::LogPage& brought)
	{
		const std::size_t count = brought.Commits.size();

		return Core::FormatCount(count) + (brought.Complete ? "" : "+") + (count == 1 && brought.Complete ? " commit" : " commits");
	}

	CommitRows BuildCommitRows(std::size_t commitCount,
							   const std::function<std::size_t(std::size_t)>& fileRowsUnder,
							   const ExtraRows& extra)
	{
		CommitRows rows;
		rows.RowOfCommit.reserve(commitCount);
		rows.Rows.reserve(commitCount + 2);

		if (extra.LocalChanges)
		{
			rows.Rows.push_back(RowOfKind(CommitRowKind::LocalChanges));
		}

		if (commitCount == 0 && (extra.LocalChanges || extra.Waiting))
		{
			rows.Rows.push_back(RowOfKind(CommitRowKind::Note));
		}

		for (std::size_t commit = 0; commit < commitCount; ++commit)
		{
			if (extra.FoldBeforeCommit == commit)
			{
				rows.Rows.push_back(RowOfKind(CommitRowKind::Fold, commit));
			}

			rows.RowOfCommit.push_back(rows.Rows.size());
			rows.Rows.push_back(RowOfKind(CommitRowKind::Commit, commit));

			const std::size_t broughtRowCount = extra.BroughtRowsUnder ? extra.BroughtRowsUnder(commit) : 0;

			for (std::size_t brought = 0; brought < broughtRowCount; ++brought)
			{
				CommitRow broughtRow = RowOfKind(CommitRowKind::Brought, commit);
				broughtRow.File = brought;
				rows.Rows.push_back(broughtRow);
			}

			const std::size_t fileRowCount = fileRowsUnder(commit);

			for (std::size_t file = 0; file < fileRowCount; ++file)
			{
				CommitRow fileRow = RowOfKind(CommitRowKind::File, commit);
				fileRow.File = file;
				rows.Rows.push_back(fileRow);
			}
		}

		return rows;
	}

	std::string DescribeLocalChanges(const Core::WorkingTreeStatus* status)
	{
		if (status == nullptr)
		{
			return "\xE2\x80\xA6";
		}

		if (status->Entries.empty())
		{
			return "nothing to commit";
		}

		// Counted as the Changes view lists them, so the row and the view agree.
		const ChangeSections sections = PartitionStatus(*status);
		const std::string staged = Core::FormatCount(sections.Staged.size()) + " staged \xC2\xB7 " +
								   Core::FormatCount(sections.Unstaged.size()) + " not staged";

		if (sections.Conflicted.empty())
		{
			return staged;
		}

		return Core::FormatCount(sections.Conflicted.size()) + " conflicted \xC2\xB7 " + staged;
	}
}
