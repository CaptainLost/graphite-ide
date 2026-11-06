#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>
#include <git2/sys/errors.h>
#include <git2/sys/mempack.h>
#include <git2/sys/odb_backend.h>

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		// Higher than the backends the repository opens with, so every write lands in memory.
		constexpr int kMempackPriority = 1000;

		// WARNING: The merged index is compared with the tree entry by entry, not written as a tree and its
		// id compared: writing checks that every blob it names exists, which a blobless clone fails for
		// files the merge never had to read.
		Result<ContentAnswer> IndexMatchesTree(git_repository* handle, git_index* index, git_commit* commit)
		{
			TreeHandle tree;
			git_error_clear();

			if (const int treeResult = git_commit_tree(Out(tree), commit); treeResult != 0)
			{
				if (IsMissingObject(treeResult))
				{
					return ContentAnswer::Unknown;
				}

				return Error{ErrorCode::IoFailure, LastLibgit2Message()};
			}

			DiffHandle difference;
			git_error_clear();

			if (const int diffResult = git_diff_tree_to_index(Out(difference), handle, tree.get(), index, nullptr);
				diffResult != 0)
			{
				if (IsMissingObject(diffResult))
				{
					return ContentAnswer::Unknown;
				}

				return Error{ErrorCode::IoFailure, LastLibgit2Message()};
			}

			return git_diff_num_deltas(difference.get()) == 0 ? ContentAnswer::Yes : ContentAnswer::No;
		}

		// Without rename detection a rename is a delete and an add, so both of a file's paths count.
		bool TouchesAny(git_diff* diff, const std::unordered_set<std::string>& paths)
		{
			const std::size_t deltaCount = git_diff_num_deltas(diff);

			for (std::size_t deltaIndex = 0; deltaIndex < deltaCount; ++deltaIndex)
			{
				const git_diff_delta* const delta = git_diff_get_delta(diff, deltaIndex);

				if (paths.contains(TextOrEmpty(delta->old_file.path)) || paths.contains(TextOrEmpty(delta->new_file.path)))
				{
					return true;
				}
			}

			return false;
		}

		std::unordered_set<std::string> PathsIn(git_diff* diff)
		{
			std::unordered_set<std::string> paths;
			const std::size_t deltaCount = git_diff_num_deltas(diff);

			for (std::size_t deltaIndex = 0; deltaIndex < deltaCount; ++deltaIndex)
			{
				const git_diff_delta* const delta = git_diff_get_delta(diff, deltaIndex);
				paths.insert(TextOrEmpty(delta->old_file.path));
				paths.insert(TextOrEmpty(delta->new_file.path));
			}

			return paths;
		}

		Result<TreeHandle> TreeOf(git_repository* handle, const Oid& commitId)
		{
			Result<CommitHandle> commit = LookupCommit(handle, commitId);

			if (!commit.HasValue())
			{
				return std::move(commit).GetError();
			}

			TreeHandle tree;
			git_error_clear();

			if (const int treeResult = git_commit_tree(Out(tree), commit.GetValue().get()); treeResult != 0)
			{
				return ObjectReadFailure(treeResult, std::format("cannot read the tree of {}", commitId.ShortHex()));
			}

			return tree;
		}

		Result<std::unordered_set<std::string>> PathsChangedBetween(git_repository* handle, const Oid& older,
																	const Oid& newer)
		{
			Result<TreeHandle> olderTree = TreeOf(handle, older);

			if (!olderTree.HasValue())
			{
				return std::move(olderTree).GetError();
			}

			Result<TreeHandle> newerTree = TreeOf(handle, newer);

			if (!newerTree.HasValue())
			{
				return std::move(newerTree).GetError();
			}

			DiffHandle diff;
			git_error_clear();

			if (const int diffResult =
					git_diff_tree_to_tree(Out(diff), handle, olderTree.GetValue().get(), newerTree.GetValue().get(), nullptr);
				diffResult != 0)
			{
				return ObjectReadFailure(diffResult, std::format("cannot compare {} with {}", older.ShortHex(), newer.ShortHex()));
			}

			return PathsIn(diff.get());
		}

		Result<bool> CommitTouchesAny(git_repository* handle, const Oid& commitId, const std::unordered_set<std::string>& paths)
		{
			Result<CommitHandle> commit = LookupCommit(handle, commitId);

			if (!commit.HasValue())
			{
				return std::move(commit).GetError();
			}

			Result<DiffHandle> diff = DiffWithFirstParent(handle, commit.GetValue().get());

			if (!diff.HasValue())
			{
				return std::move(diff).GetError();
			}

			return TouchesAny(diff.GetValue().get(), paths);
		}

		Result<RevwalkHandle> StartFirstParentWalkOldestFirst(git_repository* handle, const Oid& tip, const Oid& since)
		{
			RevwalkHandle walk;

			if (git_revwalk_new(Out(walk), handle) != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot walk the line: {}", LastLibgit2Message())};
			}

			const git_oid tipId = ToGitOid(tip);
			const git_oid sinceId = ToGitOid(since);

			if (git_revwalk_sorting(walk.get(), GIT_SORT_REVERSE) != 0 || git_revwalk_simplify_first_parent(walk.get()) != 0 ||
				git_revwalk_push(walk.get(), &tipId) != 0 || git_revwalk_hide(walk.get(), &sinceId) != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot walk the line: {}", LastLibgit2Message())};
			}

			return walk;
		}
	}

	Result<void> Repository::AttachMemoryBackend()
	{
		git_odb* database = nullptr;

		if (git_repository_odb(&database, m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure, "cannot open the object database: " + LastLibgit2Message()};
		}

		git_odb_backend* backend = nullptr;

		if (git_mempack_new(&backend) != 0)
		{
			git_odb_free(database);
			return Error{ErrorCode::IoFailure, "cannot create an in-memory object store: " + LastLibgit2Message()};
		}

		const int addResult = git_odb_add_backend(database, backend, kMempackPriority);
		git_odb_free(database);

		// The database owns the backend only once adding it succeeded.
		if (addResult != 0)
		{
			const std::string message = LastLibgit2Message();
			backend->free(backend);
			return Error{ErrorCode::IoFailure, "cannot add an in-memory object store: " + message};
		}

		m_MemoryBackend = backend;
		return Result<void>{};
	}

	void Repository::DropObjectsInMemory() const
	{
		// A moved-from repository keeps the pointer, but the database that owned the backend went with
		// the handle.
		if (m_Handle != nullptr && m_MemoryBackend != nullptr)
		{
			git_mempack_reset(m_MemoryBackend);
		}
	}

	Result<ContentAnswer> Repository::MergeChangesNothing(const Oid& lineTip, const Oid& branchTip) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		if (m_MemoryBackend == nullptr)
		{
			return Error{ErrorCode::InvalidArgument, "the repository was not opened to keep its objects in memory"};
		}

		Result<CommitHandle> line = LookupCommit(m_Handle.get(), lineTip);
		Result<CommitHandle> branch = LookupCommit(m_Handle.get(), branchTip);

		if (!line.HasValue() || !branch.HasValue())
		{
			return ContentAnswer::Unknown;
		}

		git_merge_options options = GIT_MERGE_OPTIONS_INIT;
		options.flags = GIT_MERGE_FAIL_ON_CONFLICT | GIT_MERGE_SKIP_REUC;

		IndexHandle merged;
		git_error_clear();
		const int mergeResult =
			git_merge_commits(Out(merged), m_Handle.get(), line.GetValue().get(), branch.GetValue().get(), &options);

		ContentAnswer answer = ContentAnswer::No;
		std::optional<std::string> failure;

		if (mergeResult == GIT_EMERGECONFLICT)
		{
			answer = ContentAnswer::No;
		}
		else if (mergeResult != 0)
		{
			if (IsMissingObject(mergeResult))
			{
				answer = ContentAnswer::Unknown;
			}
			else
			{
				failure = LastLibgit2Message();
			}
		}
		else if (git_index_has_conflicts(merged.get()) != 0)
		{
			answer = ContentAnswer::No;
		}
		else
		{
			const Result<ContentAnswer> compared = IndexMatchesTree(m_Handle.get(), merged.get(), line.GetValue().get());

			if (compared.HasValue())
			{
				answer = compared.GetValue();
			}
			else
			{
				failure = compared.GetError().Message;
			}
		}

		DropObjectsInMemory();

		if (failure.has_value())
		{
			return Error{ErrorCode::IoFailure, std::format("cannot merge in memory: {}", *failure)};
		}

		return answer;
	}

	bool Repository::HoldsCommit(const Oid& commit) const
	{
		if (m_Handle == nullptr)
		{
			return false;
		}

		return LookupCommit(m_Handle.get(), commit).HasValue();
	}

	Result<bool> Repository::HaveSameTree(const Oid& one, const Oid& other) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<CommitHandle> oneCommit = LookupCommit(m_Handle.get(), one);

		if (!oneCommit.HasValue())
		{
			return std::move(oneCommit).GetError();
		}

		Result<CommitHandle> otherCommit = LookupCommit(m_Handle.get(), other);

		if (!otherCommit.HasValue())
		{
			return std::move(otherCommit).GetError();
		}

		return git_oid_equal(git_commit_tree_id(oneCommit.GetValue().get()), git_commit_tree_id(otherCommit.GetValue().get())) != 0;
	}

	Result<std::vector<Oid>> Repository::LineCommitsTouchingBranch(const Oid& lineTip, const Oid& since,
																   const Oid& branchTip) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<std::unordered_set<std::string>> branchPaths = PathsChangedBetween(m_Handle.get(), since, branchTip);

		if (!branchPaths.HasValue())
		{
			return std::move(branchPaths).GetError();
		}

		std::vector<Oid> touching;

		if (branchPaths.GetValue().empty())
		{
			return touching;
		}

		Result<RevwalkHandle> walk = StartFirstParentWalkOldestFirst(m_Handle.get(), lineTip, since);

		if (!walk.HasValue())
		{
			return std::move(walk).GetError();
		}

		git_oid walked{};
		int nextResult = 0;

		while ((nextResult = git_revwalk_next(&walked, walk.GetValue().get())) == 0)
		{
			Result<bool> touches = CommitTouchesAny(m_Handle.get(), ToOid(walked), branchPaths.GetValue());

			if (!touches.HasValue())
			{
				return std::move(touches).GetError();
			}

			if (touches.GetValue())
			{
				touching.push_back(ToOid(walked));
			}
		}

		if (nextResult != GIT_ITEROVER)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk the line: {}", LastLibgit2Message())};
		}

		return touching;
	}
}
