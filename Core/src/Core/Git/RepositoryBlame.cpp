#include "corepch.h"

#include "Core/Git/Blame.h"
#include "Core/Git/Repository.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace Core {

	const BlameHunk* HunkOfLine(const Blame& blame, std::size_t line)
	{
		if (line == 0 || line > blame.LineCount)
		{
			return nullptr;
		}

		// The first hunk that starts after the line; the one before it holds the line.
		const auto firstStartingAfter = std::upper_bound(
			blame.Hunks.begin(), blame.Hunks.end(), line,
			[](std::size_t wanted, const BlameHunk& hunk) { return wanted < hunk.FirstLine; });

		if (firstStartingAfter == blame.Hunks.begin())
		{
			return nullptr;
		}

		const BlameHunk& candidate = *(firstStartingAfter - 1);

		return line < candidate.FirstLine + candidate.LineCount ? &candidate : nullptr;
	}

	Result<Blame> Repository::QueryBlame(const BlameRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		const std::string gitPath = ToGenericUtf8(request.File);

		// git_blame_file reports a path missing from the commit as a generic error, so the tree is
		// asked first to tell an ordinary "no such file" from a failure.
		Result<CommitHandle> lookedUp = LookupCommit(m_Handle.get(), request.At);

		if (!lookedUp.HasValue())
		{
			return lookedUp.GetError();
		}

		const CommitHandle atCommit = std::move(lookedUp).GetValue();

		TreeHandle tree;
		if (git_commit_tree(Out(tree), atCommit.get()) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the commit's tree: {}", LastLibgit2Message())};
		}

		TreeEntryHandle entry;
		const int entryResult = git_tree_entry_bypath(Out(entry), tree.get(), gitPath.c_str());

		if (entryResult != 0 && entryResult != GIT_ENOTFOUND)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot find '{}': {}", gitPath, LastLibgit2Message())};
		}

		const bool isFile = entryResult == 0 && git_tree_entry_type(entry.get()) == GIT_OBJECT_BLOB;

		if (!isFile)
		{
			return Error{ErrorCode::NotFound,
						 std::format("'{}' is not a file in commit {}", gitPath, request.At.ShortHex())};
		}

		git_blame_options options = GIT_BLAME_OPTIONS_INIT;
		options.flags = GIT_BLAME_USE_MAILMAP;
		options.newest_commit = ToGitOid(request.At);

		BlameHandle blame;
		if (git_blame_file(Out(blame), m_Handle.get(), gitPath.c_str(), &options) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot blame '{}': {}", gitPath, LastLibgit2Message())};
		}

		Blame result;
		result.At = request.At;
		result.File = request.File;

		const std::uint32_t hunkCount = git_blame_get_hunk_count(blame.get());

		for (std::uint32_t index = 0; index < hunkCount; ++index)
		{
			const git_blame_hunk* hunk = git_blame_get_hunk_byindex(blame.get(), index);

			BlameHunk converted;
			converted.FirstLine = hunk->final_start_line_number;
			converted.LineCount = hunk->lines_in_hunk;
			converted.Commit = ToOid(hunk->final_commit_id);
			converted.OriginalPath = FromUtf8(TextOrEmpty(hunk->orig_path));
			converted.OriginalFirstLine = hunk->orig_start_line_number;

			result.LineCount = std::max(result.LineCount, converted.FirstLine + converted.LineCount - 1);

			if (!result.Commits.contains(converted.Commit))
			{
				CommitHandle commit;
				if (git_commit_lookup(Out(commit), m_Handle.get(), &hunk->final_commit_id) != 0)
				{
					return Error{ErrorCode::IoFailure,
								 std::format("cannot read commit {}: {}", converted.Commit.ShortHex(),
											 LastLibgit2Message())};
				}

				CommitSummary summary = ToCommitSummary(commit.get());

				if (hunk->final_signature != nullptr)
				{
					summary.Author = ToSignature(hunk->final_signature);
				}

				result.Commits.emplace(converted.Commit, std::move(summary));
			}

			result.Hunks.push_back(std::move(converted));
		}

		return result;
	}
}
