#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>
#include <git2/sys/errors.h>
#include <git2/sys/mempack.h>
#include <git2/sys/odb_backend.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <queue>
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

		struct DatedCommit
		{
			std::int64_t Time = 0;
			Oid Id;
		};

		struct IsCommittedEarlier
		{
			bool operator()(const DatedCommit& left, const DatedCommit& right) const
			{
				return left.Time < right.Time;
			}
		};

		// The commits one commit reaches, found newest first and only as far back in time as a question
		// needs, so a walk that stops early reads no more of that history than it asked about. A libgit2
		// walk that hides a commit reads all of what it shows before it shows the first.
		// WARNING: It trusts a commit to be dated no earlier than its parents. A parent dated after its
		// child is found a few commits late, which can only add commits from before the fork to a landing
		// search, and merging the branch into one of those never changes nothing.
		class CommitsReachedFrom
		{
		public:
			CommitsReachedFrom(git_repository* handle, const DatedCommit& start)
				: m_Handle(handle)
			{
				m_Queued.insert(start.Id);
				m_Frontier.push(start);
			}

			Result<bool> Reaches(const DatedCommit& commit)
			{
				while (!m_Frontier.empty() && m_Frontier.top().Time >= commit.Time)
				{
					const DatedCommit next = m_Frontier.top();
					m_Frontier.pop();
					m_Reached.insert(next.Id);

					if (const Result<void> queued = QueueParentsOf(next.Id); !queued.HasValue())
					{
						return queued.GetError();
					}
				}

				return m_Reached.contains(commit.Id);
			}

		private:
			Result<void> QueueParentsOf(const Oid& commitId)
			{
				Result<CommitHandle> commit = LookupCommit(m_Handle, commitId);

				if (!commit.HasValue())
				{
					return commit.GetError();
				}

				const unsigned int parentCount = git_commit_parentcount(commit.GetValue().get());

				for (unsigned int parentIndex = 0; parentIndex < parentCount; ++parentIndex)
				{
					const Oid parentId = ToOid(*git_commit_parent_id(commit.GetValue().get(), parentIndex));

					if (m_Queued.contains(parentId))
					{
						continue;
					}

					Result<CommitHandle> parent = LookupCommit(m_Handle, parentId);

					if (!parent.HasValue())
					{
						return parent.GetError();
					}

					m_Queued.insert(parentId);
					m_Frontier.push(DatedCommit{.Time = git_commit_time(parent.GetValue().get()), .Id = parentId});
				}

				return Result<void>{};
			}

			git_repository* m_Handle = nullptr;
			std::priority_queue<DatedCommit, std::vector<DatedCommit>, IsCommittedEarlier> m_Frontier;
			std::unordered_set<Oid> m_Queued;
			std::unordered_set<Oid> m_Reached;
		};

		std::optional<Oid> FirstParentIdOf(git_commit* commit)
		{
			if (git_commit_parentcount(commit) == 0)
			{
				return std::nullopt;
			}

			return ToOid(*git_commit_parent_id(commit, kFirstParentIndex));
		}

		// tip's first-parent chain down to where `since` reaches it, newest first. Empty optional when the
		// chain is longer than `budget` commits.
		Result<std::optional<std::vector<Oid>>> FirstParentChainSince(git_repository* handle, const Oid& tip,
																	   const Oid& since, std::size_t budget)
		{
			Result<CommitHandle> sinceCommit = LookupCommit(handle, since);

			if (!sinceCommit.HasValue())
			{
				return sinceCommit.GetError();
			}

			CommitsReachedFrom reachedFromSince(
				handle, DatedCommit{.Time = git_commit_time(sinceCommit.GetValue().get()), .Id = since});

			const std::optional<std::vector<Oid>> longerThanTheBudget;
			std::vector<Oid> chain;
			std::optional<Oid> next = tip;

			while (next.has_value())
			{
				Result<CommitHandle> commit = LookupCommit(handle, *next);

				if (!commit.HasValue())
				{
					return commit.GetError();
				}

				const DatedCommit dated{.Time = git_commit_time(commit.GetValue().get()), .Id = *next};
				const Result<bool> reached = reachedFromSince.Reaches(dated);

				if (!reached.HasValue())
				{
					return reached.GetError();
				}

				if (reached.GetValue())
				{
					break;
				}

				if (chain.size() == budget)
				{
					return longerThanTheBudget;
				}

				chain.push_back(*next);
				next = FirstParentIdOf(commit.GetValue().get());
			}

			return std::optional<std::vector<Oid>>{std::move(chain)};
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

	Result<bool> Repository::HoldsCommit(const Oid& commit) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
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
														   const Oid& branchTip, std::size_t chainBudget,
														   std::size_t mostCommits) const
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

		if (branchPaths.GetValue().empty() || mostCommits == 0)
		{
			return touching;
		}

		Result<std::optional<std::vector<Oid>>> chain = FirstParentChainSince(m_Handle.get(), lineTip, since, chainBudget);

		if (!chain.HasValue())
		{
			return std::move(chain).GetError();
		}

		if (!chain.GetValue().has_value())
		{
			return touching;
		}

		std::vector<Oid> oldestFirst = std::move(*chain.GetValue());
		std::reverse(oldestFirst.begin(), oldestFirst.end());

		for (const Oid& commit : oldestFirst)
		{
			const Result<bool> touches = CommitTouchesAny(m_Handle.get(), commit, branchPaths.GetValue());

			if (!touches.HasValue())
			{
				return touches.GetError();
			}

			if (touches.GetValue())
			{
				touching.push_back(commit);
			}

			if (touching.size() == mostCommits)
			{
				break;
			}
		}

		return touching;
	}
}
