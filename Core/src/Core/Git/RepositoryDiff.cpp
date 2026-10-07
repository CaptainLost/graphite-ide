#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstdint>
#include <format>
#include <string>

namespace Core {

	namespace {

		// A file that was never added is a change the status listing reports, and without
		// SHOW_UNTRACKED_CONTENT its delta has no hunks, which reads exactly like no change. Without
		// RECURSE_UNTRACKED_DIRS a pathspec naming a file inside an untracked directory matches
		// nothing, because the directory stands in for everything under it.
		constexpr std::uint32_t kUntrackedFilesWithContent =
			GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT | GIT_DIFF_RECURSE_UNTRACKED_DIRS;

		FileChange ToFileChange(git_delta_t deltaStatus)
		{
			switch (deltaStatus)
			{
			case GIT_DELTA_ADDED:
				return FileChange::Added;
			case GIT_DELTA_DELETED:
				return FileChange::Deleted;
			case GIT_DELTA_MODIFIED:
				return FileChange::Modified;
			case GIT_DELTA_RENAMED:
				return FileChange::Renamed;
			case GIT_DELTA_COPIED:
				return FileChange::Copied;
			case GIT_DELTA_TYPECHANGE:
				return FileChange::TypeChanged;

			// libgit2 tells an untracked file apart for a status listing's sake. To a diff it is a
			// file that is entirely new, and a view that only knew Added would draw it as empty.
			case GIT_DELTA_UNTRACKED:
				return FileChange::Added;

			default:
				return FileChange::Unmodified;
			}
		}

		// libgit2 numbers the side a line does not exist on as -1.
		int LineNumberOrZeroWhenAbsent(int libgit2LineNumber)
		{
			return libgit2LineNumber > 0 ? libgit2LineNumber : 0;
		}

		// libgit2 calls the three Append callbacks in file, hunk, line order, so the file and hunk
		// being filled are always the last ones appended.
		int AppendFile(const git_diff_delta* delta, float, void* payload)
		{
			Diff& collected = *static_cast<Diff*>(payload);

			FileDiff file;
			file.Change = ToFileChange(delta->status);
			file.IsBinary = (delta->flags & GIT_DIFF_FLAG_BINARY) != 0;

			// Tested against the converted change rather than delta->status, so an untracked delta
			// keeps the empty OldPath that FileDiff promises for an add.
			if (delta->old_file.path != nullptr && file.Change != FileChange::Added)
			{
				file.OldPath = FromUtf8(delta->old_file.path);
			}

			if (delta->new_file.path != nullptr && file.Change != FileChange::Deleted)
			{
				file.NewPath = FromUtf8(delta->new_file.path);
			}

			collected.Files.push_back(std::move(file));
			return 0;
		}

		int AppendHunk(const git_diff_delta*, const git_diff_hunk* hunk, void* payload)
		{
			Diff& collected = *static_cast<Diff*>(payload);

			if (collected.Files.empty())
			{
				return 0;
			}

			Hunk entry;
			entry.OldStart = hunk->old_start;
			entry.OldCount = hunk->old_lines;
			entry.NewStart = hunk->new_start;
			entry.NewCount = hunk->new_lines;
			entry.Header.assign(hunk->header, hunk->header_len);

			collected.Files.back().Hunks.push_back(std::move(entry));
			return 0;
		}

		int AppendLine(const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line, void* payload)
		{
			Diff& collected = *static_cast<Diff*>(payload);

			if (collected.Files.empty() || collected.Files.back().Hunks.empty())
			{
				return 0;
			}

			DiffLine entry;

			switch (line->origin)
			{
			case GIT_DIFF_LINE_ADDITION:
				entry.Kind = DiffLineKind::Added;
				break;
			case GIT_DIFF_LINE_DELETION:
				entry.Kind = DiffLineKind::Removed;
				break;
			case GIT_DIFF_LINE_CONTEXT:
				entry.Kind = DiffLineKind::Context;
				break;

			// libgit2 also reports end-of-file-newline markers, which carry no line of the file and
			// would draw a phantom line in the view.
			default:
				return 0;
			}

			entry.OldLine = LineNumberOrZeroWhenAbsent(line->old_lineno);
			entry.NewLine = LineNumberOrZeroWhenAbsent(line->new_lineno);
			entry.Text.assign(line->content, line->content_len);

			collected.Files.back().Hunks.back().Lines.push_back(std::move(entry));
			return 0;
		}

		// A null tree is the empty tree to libgit2, so a lookup that merely failed must never come
		// back as one: it is reported, or the diff would read as every file added or deleted.
		Result<TreeHandle> TreeOfCommit(git_commit* commit)
		{
			TreeHandle tree;

			if (git_commit_tree(Out(tree), commit) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read a commit's tree: {}", LastLibgit2Message())};
			}

			return tree;
		}

		Result<TreeHandle> TreeOfCommitIdOrEmptyWhenNone(git_repository* handle,
														 const std::optional<Oid>& commitId)
		{
			if (!commitId.has_value())
			{
				return TreeHandle{};
			}

			git_oid gitCommitId = ToGitOid(*commitId);

			CommitHandle commit;
			if (git_commit_lookup(Out(commit), handle, &gitCommitId) != 0)
			{
				return Error{ErrorCode::NotFound,
							 std::format("cannot find commit {}: {}", commitId->ToHex(), LastLibgit2Message())};
			}

			return TreeOfCommit(commit.get());
		}

		Result<TreeHandle> HeadTreeOrEmptyWhenUnborn(git_repository* handle)
		{
			const int unbornResult = git_repository_head_unborn(handle);

			if (unbornResult < 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot read HEAD: {}", LastLibgit2Message())};
			}

			if (unbornResult == kHeadIsUnborn)
			{
				return TreeHandle{};
			}

			git_oid headId{};

			if (git_reference_name_to_id(&headId, handle, "HEAD") != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot read HEAD: {}", LastLibgit2Message())};
			}

			CommitHandle commit;
			if (git_commit_lookup(Out(commit), handle, &headId) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read the HEAD commit: {}", LastLibgit2Message())};
			}

			return TreeOfCommit(commit.get());
		}
	}

	Result<Diff> Repository::QueryDiff(const DiffRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_diff_options options = GIT_DIFF_OPTIONS_INIT;

		const bool comparesWorkTree =
			request.Target == DiffTarget::WorkTreeToIndex || request.Target == DiffTarget::WorkTreeToHead;

		if (comparesWorkTree)
		{
			options.flags |= kUntrackedFilesWithContent;
		}

		std::vector<std::filesystem::path> pathspecPaths;

		if (request.File.has_value())
		{
			pathspecPaths.push_back(*request.File);
		}

		const PathSpec pathspec{pathspecPaths};

		if (request.File.has_value())
		{
			options.pathspec = pathspec.Get();
		}

		// Declared before the diff, so the diff is freed before the trees it was made from.
		TreeHandle oldTree;
		TreeHandle newTree;
		TreeHandle untrackedTree;
		DiffHandle untrackedDiff;
		DiffHandle diff;
		int diffResult = -1;
		git_index* useRepositoryIndex = nullptr;

		switch (request.Target)
		{
		case DiffTarget::WorkTreeToIndex:
			diffResult = git_diff_index_to_workdir(Out(diff), m_Handle.get(), useRepositoryIndex, &options);
			break;

		case DiffTarget::IndexToHead:
		case DiffTarget::WorkTreeToHead:
		{
			Result<TreeHandle> headTree = HeadTreeOrEmptyWhenUnborn(m_Handle.get());

			if (!headTree.HasValue())
			{
				return headTree.GetError();
			}

			oldTree = std::move(headTree).GetValue();

			if (request.Target == DiffTarget::IndexToHead)
			{
				diffResult = git_diff_tree_to_index(Out(diff), m_Handle.get(), oldTree.get(), useRepositoryIndex, &options);
			}
			else
			{
				diffResult = git_diff_tree_to_workdir_with_index(Out(diff), m_Handle.get(), oldTree.get(), &options);
			}
			break;
		}

		case DiffTarget::CommitToParent:
		case DiffTarget::StashToParent:
		{
			if (!request.From.has_value())
			{
				return Error{ErrorCode::InvalidArgument, "a commit-to-parent diff needs a commit"};
			}

			git_oid commitId = ToGitOid(*request.From);

			CommitHandle commit;
			if (git_commit_lookup(Out(commit), m_Handle.get(), &commitId) != 0)
			{
				return Error{ErrorCode::NotFound,
							 std::format("cannot find commit {}: {}", request.From->ToHex(), LastLibgit2Message())};
			}

			Result<TreeHandle> commitTree = TreeOfCommit(commit.get());

			if (!commitTree.HasValue())
			{
				return commitTree.GetError();
			}

			newTree = std::move(commitTree).GetValue();

			// A merge diffed against every parent at once reads as a diff nobody wrote, so only the
			// first parent is used. A root commit is diffed against nothing, but a parent that
			// exists and cannot be read is an error.
			const bool isRootCommit = git_commit_parentcount(commit.get()) == 0;

			if (!isRootCommit)
			{
				CommitHandle parent;

				if (git_commit_parent(Out(parent), commit.get(), kFirstParentIndex) != 0)
				{
					const std::string message = LastLibgit2Message();
					return Error{ErrorCode::IoFailure,
								 std::format("cannot read the parent of {}: {}", request.From->ToHex(), message)};
				}

				Result<TreeHandle> parentTree = TreeOfCommit(parent.get());

				if (!parentTree.HasValue())
				{
					return parentTree.GetError();
				}

				oldTree = std::move(parentTree).GetValue();
			}

			diffResult = git_diff_tree_to_tree(Out(diff), m_Handle.get(), oldTree.get(), newTree.get(), &options);

			const bool isStashWithUntrackedFiles =
				request.Target == DiffTarget::StashToParent &&
				git_commit_parentcount(commit.get()) > kUntrackedFilesParentOfStash;

			if (diffResult == 0 && isStashWithUntrackedFiles)
			{
				CommitHandle untrackedFilesCommit;

				if (git_commit_parent(Out(untrackedFilesCommit), commit.get(), kUntrackedFilesParentOfStash) != 0)
				{
					const std::string message = LastLibgit2Message();
					return Error{ErrorCode::IoFailure, std::format("cannot read the untracked files of stash {}: {}",
																   request.From->ToHex(), message)};
				}

				Result<TreeHandle> untrackedFilesTree = TreeOfCommit(untrackedFilesCommit.get());

				if (!untrackedFilesTree.HasValue())
				{
					return untrackedFilesTree.GetError();
				}

				untrackedTree = std::move(untrackedFilesTree).GetValue();

				git_tree* emptyOldTree = nullptr;
				diffResult = git_diff_tree_to_tree(Out(untrackedDiff), m_Handle.get(), emptyOldTree,
												   untrackedTree.get(), &options);

				if (diffResult == 0)
				{
					diffResult = git_diff_merge(diff.get(), untrackedDiff.get());
				}
			}

			break;
		}

		case DiffTarget::RevisionToRevision:
		{
			Result<TreeHandle> fromTree = TreeOfCommitIdOrEmptyWhenNone(m_Handle.get(), request.From);

			if (!fromTree.HasValue())
			{
				return fromTree.GetError();
			}

			oldTree = std::move(fromTree).GetValue();

			Result<TreeHandle> toTree = TreeOfCommitIdOrEmptyWhenNone(m_Handle.get(), request.To);

			if (!toTree.HasValue())
			{
				return toTree.GetError();
			}

			newTree = std::move(toTree).GetValue();
			diffResult = git_diff_tree_to_tree(Out(diff), m_Handle.get(), oldTree.get(), newTree.get(), &options);
			break;
		}
		}

		if (diffResult != 0 || diff == nullptr)
		{
			const std::string message = LastLibgit2Message();

			return Error{ErrorCode::IoFailure, std::format("cannot diff: {}", message)};
		}

		git_diff_find_options findOptions = GIT_DIFF_FIND_OPTIONS_INIT;
		findOptions.flags = GIT_DIFF_FIND_RENAMES | GIT_DIFF_FIND_COPIES;

		if (git_diff_find_similar(diff.get(), &findOptions) != 0)
		{
			const std::string message = LastLibgit2Message();

			return Error{ErrorCode::IoFailure, std::format("cannot detect renames: {}", message)};
		}

		Diff collected;
		git_diff_binary_cb noBinaryCallback = nullptr;
		const int foreachResult =
			git_diff_foreach(diff.get(), AppendFile, noBinaryCallback, AppendHunk, AppendLine, &collected);

		if (foreachResult != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read the diff: {}", LastLibgit2Message())};
		}

		return collected;
	}
}
