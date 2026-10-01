#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <string>

namespace Core {

	namespace {

		FileChange ToChange(git_delta_t status)
		{
			switch (status)
			{
				case GIT_DELTA_ADDED: return FileChange::Added;
				case GIT_DELTA_DELETED: return FileChange::Deleted;
				case GIT_DELTA_MODIFIED: return FileChange::Modified;
				case GIT_DELTA_RENAMED: return FileChange::Renamed;
				case GIT_DELTA_COPIED: return FileChange::Copied;
				case GIT_DELTA_TYPECHANGE: return FileChange::TypeChanged;

				// A file git has never seen is, against the index or against HEAD, a file
				// that is entirely new - which is what an add is. libgit2 keeps the two
				// deltas apart because a status listing wants to say "untracked"; a diff
				// does not, and a view that only knew Added would otherwise draw the file
				// as unmodified and empty.
				case GIT_DELTA_UNTRACKED: return FileChange::Added;

				default: return FileChange::Unmodified;
			}
		}

		// Collected by the three callbacks below, which libgit2 calls in file, hunk, line
		// order — so the open file is always the last one appended.
		struct Collector
		{
			Diff Result;
		};

		int OnFile(const git_diff_delta* delta, float, void* payload)
		{
			Collector& collector = *static_cast<Collector*>(payload);

			FileDiff file;
			file.Change = ToChange(delta->status);
			file.IsBinary = (delta->flags & GIT_DIFF_FLAG_BINARY) != 0;

			// Tested against the change rather than against delta->status, so that an
			// untracked delta - an add by any other name - keeps the empty OldPath that
			// FileDiff promises for one. Readers of that path go looking for a blob on the
			// old side, and for a file git has never seen there is none.
			if (delta->old_file.path != nullptr && file.Change != FileChange::Added)
			{
				file.OldPath = FromUtf8(delta->old_file.path);
			}

			if (delta->new_file.path != nullptr && file.Change != FileChange::Deleted)
			{
				file.NewPath = FromUtf8(delta->new_file.path);
			}

			collector.Result.Files.push_back(std::move(file));
			return 0;
		}

		int OnHunk(const git_diff_delta*, const git_diff_hunk* hunk, void* payload)
		{
			Collector& collector = *static_cast<Collector*>(payload);

			if (collector.Result.Files.empty())
			{
				return 0;
			}

			Hunk entry;
			entry.OldStart = hunk->old_start;
			entry.OldCount = hunk->old_lines;
			entry.NewStart = hunk->new_start;
			entry.NewCount = hunk->new_lines;
			entry.Header.assign(hunk->header, hunk->header_len);

			collector.Result.Files.back().Hunks.push_back(std::move(entry));
			return 0;
		}

		int OnLine(const git_diff_delta*, const git_diff_hunk*, const git_diff_line* line,
				   void* payload)
		{
			Collector& collector = *static_cast<Collector*>(payload);

			if (collector.Result.Files.empty() || collector.Result.Files.back().Hunks.empty())
			{
				return 0;
			}

			DiffLine entry;

			switch (line->origin)
			{
				case GIT_DIFF_LINE_ADDITION: entry.Kind = DiffLineKind::Added; break;
				case GIT_DIFF_LINE_DELETION: entry.Kind = DiffLineKind::Removed; break;
				case GIT_DIFF_LINE_CONTEXT: entry.Kind = DiffLineKind::Context; break;

				// The end-of-file-newline markers carry no line of the file. Letting them
				// through would draw a phantom line in the view.
				default: return 0;
			}

			entry.OldLine = line->old_lineno > 0 ? line->old_lineno : 0;
			entry.NewLine = line->new_lineno > 0 ? line->new_lineno : 0;
			entry.Text.assign(line->content, line->content_len);

			collector.Result.Files.back().Hunks.back().Lines.push_back(std::move(entry));
			return 0;
		}
	}

	Result<Diff> Repository::QueryDiff(const DiffRequest& request) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_diff_options options = GIT_DIFF_OPTIONS_INIT;

		if (request.Target == DiffTarget::WorkTreeToIndex
			|| request.Target == DiffTarget::WorkTreeToHead)
		{
			// A new file that has never been added is a change to the working tree, and the
			// status listing already reports it as one - so a tab opened from that listing
			// must find it here too, content and all. Without SHOW_UNTRACKED_CONTENT the
			// delta arrives with no hunks, which reads on screen exactly like no change at
			// all. RECURSE_UNTRACKED_DIRS goes with it because the file wanted may sit
			// inside a directory git has never seen, and a pathspec naming that file matches
			// nothing while the directory stands in for everything under it.
			options.flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT
							 | GIT_DIFF_RECURSE_UNTRACKED_DIRS;
		}

		std::string pathspec;
		const char* pathspecEntry = nullptr;

		if (request.File.has_value())
		{
			pathspec = ToGitPath(*request.File);
			pathspecEntry = pathspec.c_str();
			options.pathspec.strings = const_cast<char**>(&pathspecEntry);
			options.pathspec.count = 1;
		}

		// The tree of a commit, or null for a side that is genuinely absent. A null tree is
		// the empty tree to libgit2, so a lookup that merely failed must never come back as
		// one: it is reported, or the diff would read as every file added or deleted.
		const auto treeOfCommit = [](git_commit* commit) -> Result<TreeHandle> {
			TreeHandle tree;

			if (git_commit_tree(Out(tree), commit) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read a commit's tree: {}", LastLibgit2Message())};
			}

			return tree;
		};

		const auto treeOf = [this, &treeOfCommit](const std::optional<Oid>& id) -> Result<TreeHandle> {
			if (!id.has_value())
			{
				return TreeHandle{};
			}

			git_oid raw = ToGitOid(*id);

			CommitHandle commit;
			if (git_commit_lookup(Out(commit), m_Handle, &raw) != 0)
			{
				return Error{ErrorCode::NotFound,
							 std::format("cannot find commit {}: {}", id->ToHex(),
										 LastLibgit2Message())};
			}

			return treeOfCommit(commit.get());
		};

		// An unborn HEAD has no tree, and that is an answer: the index is then all new.
		// Any other failure to read HEAD is not.
		const auto headTree = [this, &treeOfCommit]() -> Result<TreeHandle> {
			const int unborn = git_repository_head_unborn(m_Handle);

			if (unborn < 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read HEAD: {}", LastLibgit2Message())};
			}

			if (unborn == 1)
			{
				return TreeHandle{};
			}

			git_oid headId{};

			if (git_reference_name_to_id(&headId, m_Handle, "HEAD") != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read HEAD: {}", LastLibgit2Message())};
			}

			CommitHandle commit;
			if (git_commit_lookup(Out(commit), m_Handle, &headId) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read the HEAD commit: {}", LastLibgit2Message())};
			}

			return treeOfCommit(commit.get());
		};

		// Declared before the diff, so the diff is freed before the trees it was made from.
		TreeHandle left;
		TreeHandle right;
		DiffHandle diff;
		int created = -1;

		switch (request.Target)
		{
			case DiffTarget::WorkTreeToIndex:
				created = git_diff_index_to_workdir(Out(diff), m_Handle, nullptr, &options);
				break;

			case DiffTarget::IndexToHead:
			case DiffTarget::WorkTreeToHead:
			{
				Result<TreeHandle> head = headTree();

				if (!head.HasValue())
				{
					return head.GetError();
				}

				left = std::move(head).GetValue();
				created = request.Target == DiffTarget::IndexToHead
							  ? git_diff_tree_to_index(Out(diff), m_Handle, left.get(), nullptr, &options)
							  : git_diff_tree_to_workdir_with_index(Out(diff), m_Handle, left.get(), &options);
				break;
			}

			case DiffTarget::CommitToParent:
			{
				if (!request.From.has_value())
				{
					return Error{ErrorCode::InvalidArgument,
								 "a commit-to-parent diff needs a commit"};
				}

				git_oid raw = ToGitOid(*request.From);

				CommitHandle commit;
				if (git_commit_lookup(Out(commit), m_Handle, &raw) != 0)
				{
					return Error{ErrorCode::NotFound,
								 std::format("cannot find commit {}: {}", request.From->ToHex(),
											 LastLibgit2Message())};
				}

				Result<TreeHandle> commitTree = treeOfCommit(commit.get());

				if (!commitTree.HasValue())
				{
					return commitTree.GetError();
				}

				right = std::move(commitTree).GetValue();

				// First parent only. A merge shown against every parent at once reads
				// as a diff nobody wrote; the view offers the other parents separately.
				// A root commit has no parent, and diffing it against nothing is the
				// right answer; a parent that exists but cannot be read is not.
				if (git_commit_parentcount(commit.get()) > 0)
				{
					CommitHandle parent;

					if (git_commit_parent(Out(parent), commit.get(), 0) != 0)
					{
						const std::string message = LastLibgit2Message();
						return Error{ErrorCode::IoFailure,
									 std::format("cannot read the parent of {}: {}",
												 request.From->ToHex(), message)};
					}

					Result<TreeHandle> parentTree = treeOfCommit(parent.get());

					if (!parentTree.HasValue())
					{
						return parentTree.GetError();
					}

					left = std::move(parentTree).GetValue();
				}

				created = git_diff_tree_to_tree(Out(diff), m_Handle, left.get(), right.get(), &options);
				break;
			}

			case DiffTarget::RevisionToRevision:
			{
				Result<TreeHandle> from = treeOf(request.From);

				if (!from.HasValue())
				{
					return from.GetError();
				}

				left = std::move(from).GetValue();

				Result<TreeHandle> to = treeOf(request.To);

				if (!to.HasValue())
				{
					return to.GetError();
				}

				right = std::move(to).GetValue();
				created = git_diff_tree_to_tree(Out(diff), m_Handle, left.get(), right.get(), &options);
				break;
			}
		}

		if (created != 0 || diff == nullptr)
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

		Collector collector;
		const int walked = git_diff_foreach(diff.get(), OnFile, nullptr, OnHunk, OnLine, &collector);

		if (walked != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the diff: {}", LastLibgit2Message())};
		}

		return std::move(collector.Result);
	}
}
