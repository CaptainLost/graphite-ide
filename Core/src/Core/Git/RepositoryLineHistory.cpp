#include "corepch.h"

#include "Core/Git/LineHistory.h"
#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/LineRangeTracking.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		std::vector<Hunk> HunksContaining(const FileDiff& file, std::size_t line)
		{
			std::vector<Hunk> containing;

			for (const Hunk& hunk : file.Hunks)
			{
				for (const DiffLine& diffLine : hunk.Lines)
				{
					if (diffLine.Kind == DiffLineKind::Added && static_cast<std::size_t>(diffLine.NewLine) == line)
					{
						containing.push_back(hunk);
						break;
					}
				}
			}

			return containing;
		}

		const FileDiff* FindFile(const Diff& diff, const std::filesystem::path& newPath)
		{
			for (const FileDiff& file : diff.Files)
			{
				if (file.NewPath == newPath)
				{
					return &file;
				}
			}

			return nullptr;
		}
	}

	Result<LineHistory> Repository::QueryLineHistory(const LineHistoryRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		LineHistory history;

		Oid commit = request.From;
		std::filesystem::path path = request.File;
		std::size_t line = request.Line;
		std::size_t walked = 0;

		const auto summaryOf = [this](const Oid& id) -> Result<CommitSummary>
		{
			Result<CommitHandle> lookedUp = LookupCommit(m_Handle.get(), id);

			if (!lookedUp.HasValue())
			{
				return lookedUp.GetError();
			}

			const CommitHandle handle = std::move(lookedUp).GetValue();
			return ToCommitSummary(handle.get());
		};

		for (;;)
		{
			const Result<std::optional<Oid>> parentResult = FirstParentOf(commit);

			if (!parentResult.HasValue())
			{
				return parentResult.GetError();
			}

			const std::optional<Oid> parent = parentResult.GetValue();

			// A root commit diffs against nothing, so its file comes out as Added and ends the walk
			// below.
			const Result<Diff> fileDiff = QueryDiff(
				DiffRequest{.Target = DiffTarget::CommitToParent, .From = commit, .File = path});

			if (!fileDiff.HasValue())
			{
				history.Complete = false;
				break;
			}

			const Diff& diff = fileDiff.GetValue();

			std::optional<Diff> wholeCommit;
			const FileDiff* file = diff.Files.empty() ? nullptr : &diff.Files.front();

			if (file != nullptr && file->Change == FileChange::Added)
			{
				// The pathspec hides a rename's old side, so the file looks new; the whole commit
				// shows whether it was renamed.
				Result<Diff> whole = QueryDiff(DiffRequest{.Target = DiffTarget::CommitToParent, .From = commit});

				if (!whole.HasValue())
				{
					history.Complete = false;
					break;
				}

				wholeCommit = std::move(whole).GetValue();
				const FileDiff* renamed = FindFile(*wholeCommit, path);

				if (renamed != nullptr && renamed->Change == FileChange::Renamed)
				{
					file = renamed;
				}
			}

			if (file != nullptr)
			{
				const TracedLine traced = TraceLineToParent(*file, line);

				if (traced.Fate != LineFate::Unchanged)
				{
					Result<CommitSummary> summary = summaryOf(commit);

					if (!summary.HasValue())
					{
						return summary.GetError();
					}

					LineChange change;
					change.Commit = std::move(summary).GetValue();
					change.Path = path;
					change.Line = line;
					change.Hunks = HunksContaining(*file, line);

					if (traced.Fate == LineFate::Changed)
					{
						change.ParentLine = traced.OldLine;
					}

					history.Changes.push_back(std::move(change));

					if (traced.Fate == LineFate::Added)
					{
						break;
					}
				}

				line = traced.OldLine;

				if (file->Change == FileChange::Renamed)
				{
					path = file->OldPath;
				}
			}

			if (!parent.has_value())
			{
				// The file was not in a root commit, so the line's origin was never reached.
				history.Complete = false;
				break;
			}

			commit = *parent;
			++walked;

			if (walked >= request.MaxWalk)
			{
				history.Complete = false;
				break;
			}
		}

		return history;
	}
}
