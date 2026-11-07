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
#include <map>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
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

		bool IsTreeEntry(const git_tree_entry* entry)
		{
			return git_tree_entry_type(entry) == GIT_OBJECT_TREE;
		}

		// Valid for as long as the tree holding the entry.
		std::string_view NameOf(const git_tree_entry* entry)
		{
			const char* const name = git_tree_entry_name(entry);
			return name != nullptr ? std::string_view{name} : std::string_view{};
		}

		// The same object at the same mode: nothing under the name differs, so a tree's subtree is not read.
		bool IsSameEntry(const git_tree_entry* one, const git_tree_entry* other)
		{
			return git_tree_entry_filemode(one) == git_tree_entry_filemode(other) &&
				   git_oid_equal(git_tree_entry_id(one), git_tree_entry_id(other)) != 0;
		}

		Result<TreeHandle> SubtreeOf(git_repository* handle, const git_tree_entry* entry)
		{
			TreeHandle tree;
			git_error_clear();

			if (const int treeResult = git_tree_lookup(Out(tree), handle, git_tree_entry_id(entry)); treeResult != 0)
			{
				return ObjectReadFailure(treeResult, std::format("cannot read the tree of {}", NameOf(entry)));
			}

			return tree;
		}

		// The tree an entry names; null for no entry and for one that names a file.
		Result<TreeHandle> FolderOf(git_repository* handle, const git_tree_entry* entry)
		{
			if (entry == nullptr || !IsTreeEntry(entry))
			{
				return TreeHandle{};
			}

			return SubtreeOf(handle, entry);
		}

		// Null when the tree is null or has no entry of that name.
		const git_tree_entry* EntryNamed(git_tree* tree, std::string_view name)
		{
			return tree != nullptr ? git_tree_entry_byname(tree, std::string{name}.c_str()) : nullptr;
		}

		std::string PathIn(const std::string& folder, std::string_view name)
		{
			return folder.empty() ? std::string{name} : folder + "/" + std::string{name};
		}

		// Every file an entry stands for at `path`, a diff's way: the entry itself, or every file its tree holds.
		Result<void> AddFilesOf(git_repository* handle, const git_tree_entry* entry, const std::string& path,
								std::unordered_set<std::string>& paths)
		{
			if (!IsTreeEntry(entry))
			{
				paths.insert(path);
				return Result<void>{};
			}

			Result<TreeHandle> tree = SubtreeOf(handle, entry);

			if (!tree.HasValue())
			{
				return std::move(tree).GetError();
			}

			const std::size_t count = git_tree_entrycount(tree.GetValue().get());

			for (std::size_t index = 0; index < count; ++index)
			{
				const git_tree_entry* child = git_tree_entry_byindex(tree.GetValue().get(), index);

				if (Result<void> added = AddFilesOf(handle, child, PathIn(path, NameOf(child)), paths);
					!added.HasValue())
				{
					return added;
				}
			}

			return Result<void>{};
		}

		// The files at which the two trees of a folder differ, as a diff of them lists them. Walks only into subtrees whose
		// ids differ: a whole diff reads every tree of both. Either tree may be null, a folder only the other side has.
		Result<void> AddPathsChanged(git_repository* handle, git_tree* older, git_tree* newer, const std::string& folder,
									 std::unordered_set<std::string>& paths)
		{
			const std::size_t olderCount = older != nullptr ? git_tree_entrycount(older) : 0;

			for (std::size_t index = 0; index < olderCount; ++index)
			{
				const git_tree_entry* const before = git_tree_entry_byindex(older, index);
				const std::string_view name = NameOf(before);
				const git_tree_entry* const after = EntryNamed(newer, name);

				if (after != nullptr && IsSameEntry(before, after))
				{
					continue;
				}

				const std::string path = PathIn(folder, name);
				Result<void> added = Result<void>{};

				if (after != nullptr && IsTreeEntry(before) && IsTreeEntry(after))
				{
					Result<TreeHandle> beforeTree = SubtreeOf(handle, before);

					if (!beforeTree.HasValue())
					{
						return std::move(beforeTree).GetError();
					}

					Result<TreeHandle> afterTree = SubtreeOf(handle, after);

					if (!afterTree.HasValue())
					{
						return std::move(afterTree).GetError();
					}

					added = AddPathsChanged(handle, beforeTree.GetValue().get(), afterTree.GetValue().get(), path, paths);
				}
				else
				{
					// Gone, changed in place, or a file and a folder of one name: what either side holds there counts.
					added = AddFilesOf(handle, before, path, paths);

					if (added.HasValue() && after != nullptr)
					{
						added = AddFilesOf(handle, after, path, paths);
					}
				}

				if (!added.HasValue())
				{
					return added;
				}
			}

			const std::size_t newerCount = newer != nullptr ? git_tree_entrycount(newer) : 0;

			for (std::size_t index = 0; index < newerCount; ++index)
			{
				const git_tree_entry* const after = git_tree_entry_byindex(newer, index);
				const std::string_view name = NameOf(after);

				if (EntryNamed(older, name) != nullptr)
				{
					continue;
				}

				if (Result<void> added = AddFilesOf(handle, after, PathIn(folder, name), paths); !added.HasValue())
				{
					return added;
				}
			}

			return Result<void>{};
		}

		// The paths that name one entry of a folder, or lie under it, as the rest of each path below that entry.
		struct PathsAtName
		{
			bool NamesTheEntry = false;
			std::vector<std::string_view> Under;
		};

		std::map<std::string_view, PathsAtName> PathsByFirstName(const std::vector<std::string_view>& paths)
		{
			std::map<std::string_view, PathsAtName> byName;

			for (const std::string_view path : paths)
			{
				const std::size_t slash = path.find('/');

				if (slash == std::string_view::npos)
				{
					byName[path].NamesTheEntry = true;
				}
				else if (slash + 1 < path.size())
				{
					byName[path.substr(0, slash)].Under.push_back(path.substr(slash + 1));
				}
				else
				{
					byName[path.substr(0, slash)].NamesTheEntry = true;
				}
			}

			return byName;
		}

		// Whether the two trees of a folder differ at any of `paths`, relative to it: each names an entry and what lies
		// under it, as written. Walks only into subtrees whose ids differ and which some path lies in. Either tree may be
		// null, a folder only the other side has.
		Result<bool> DifferAt(git_repository* handle, git_tree* one, git_tree* other, const std::vector<std::string_view>& paths)
		{
			if (one != nullptr && other != nullptr && git_oid_equal(git_tree_id(one), git_tree_id(other)) != 0)
			{
				return false;
			}

			for (const auto& [name, atName] : PathsByFirstName(paths))
			{
				const git_tree_entry* const oneEntry = EntryNamed(one, name);
				const git_tree_entry* const otherEntry = EntryNamed(other, name);

				if (oneEntry == nullptr && otherEntry == nullptr)
				{
					continue;
				}

				if (oneEntry != nullptr && otherEntry != nullptr && IsSameEntry(oneEntry, otherEntry))
				{
					continue;
				}

				if (atName.NamesTheEntry)
				{
					return true;
				}

				// Only what lies under the name: nothing does under a file.
				Result<TreeHandle> oneFolder = FolderOf(handle, oneEntry);

				if (!oneFolder.HasValue())
				{
					return std::move(oneFolder).GetError();
				}

				Result<TreeHandle> otherFolder = FolderOf(handle, otherEntry);

				if (!otherFolder.HasValue())
				{
					return std::move(otherFolder).GetError();
				}

				if (oneFolder.GetValue() == nullptr && otherFolder.GetValue() == nullptr)
				{
					continue;
				}

				const Result<bool> differ = DifferAt(handle, oneFolder.GetValue().get(), otherFolder.GetValue().get(), atName.Under);

				if (!differ.HasValue() || differ.GetValue())
				{
					return differ;
				}
			}

			return false;
		}

		Result<std::unordered_set<std::string>> ChangedPathSet(git_repository* handle, const Oid& older, const Oid& newer)
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

			std::unordered_set<std::string> paths;
			const std::string root;

			if (Result<void> added = AddPathsChanged(handle, olderTree.GetValue().get(), newerTree.GetValue().get(), root, paths);
				!added.HasValue())
			{
				return std::move(added).GetError();
			}

			return paths;
		}

		// The paths a walk of two trees is kept to, as views: an empty path would name the whole tree.
		std::vector<std::string_view> NonEmptyViewsOf(const std::vector<std::string>& paths)
		{
			std::vector<std::string_view> views;
			views.reserve(paths.size());

			for (const std::string& path : paths)
			{
				if (!path.empty())
				{
					views.push_back(path);
				}
			}

			return views;
		}

		// The branch's paths as the diff of each line commit is kept to: never empty, since an empty list keeps every path.
		std::vector<std::string> PathListOf(const std::unordered_set<std::string>& paths)
		{
			std::vector<std::string> list;
			list.reserve(paths.size());

			for (const std::string& path : paths)
			{
				if (!path.empty())
				{
					list.push_back(path);
				}
			}

			return list;
		}

		// Only the trees `pathList` lies in are read: a whole first-parent diff per line commit took about 10 ms on a large
		// tree, so a landing search over thousands of line commits took half a minute.
		Result<bool> CommitTouchesAny(git_repository* handle, const Oid& commitId, const std::unordered_set<std::string>& paths,
									  const std::vector<std::string>& pathList)
		{
			Result<CommitHandle> commit = LookupCommit(handle, commitId);

			if (!commit.HasValue())
			{
				return std::move(commit).GetError();
			}

			Result<DiffHandle> diff = DiffWithFirstParentOn(handle, commit.GetValue().get(), pathList);

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

		const git_oid id = ToGitOid(commit);
		CommitHandle found;
		git_error_clear();
		const int lookupResult = git_commit_lookup(Out(found), m_Handle.get(), &id);

		if (lookupResult == 0)
		{
			return true;
		}

		// WARNING: Only a missing object is an answer. Any other failure, a read error during a repack say, would be kept on
		// disk as an unknown verdict for the pair until a tip moved.
		if (IsMissingObject(lookupResult))
		{
			return false;
		}

		return Error{ErrorCode::IoFailure, std::format("cannot read commit {}: {}", commit.ToHex(), LastLibgit2Message())};
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

		Result<std::unordered_set<std::string>> branchPaths = ChangedPathSet(m_Handle.get(), since, branchTip);

		if (!branchPaths.HasValue())
		{
			return std::move(branchPaths).GetError();
		}

		std::vector<Oid> touching;
		const std::vector<std::string> pathList = PathListOf(branchPaths.GetValue());

		if (pathList.empty() || mostCommits == 0)
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
			const Result<bool> touches = CommitTouchesAny(m_Handle.get(), commit, branchPaths.GetValue(), pathList);

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

	Result<std::vector<std::string>> Repository::PathsChangedBetween(const Oid& older, const Oid& newer) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<std::unordered_set<std::string>> changed = ChangedPathSet(m_Handle.get(), older, newer);

		if (!changed.HasValue())
		{
			return std::move(changed).GetError();
		}

		// In order, so libgit2's own sort of a path list made from it has nothing to move.
		std::vector<std::string> paths = PathListOf(changed.GetValue());
		std::sort(paths.begin(), paths.end());
		return paths;
	}

	Result<bool> Repository::TreesDifferAt(const Oid& from, const Oid& to, const std::vector<std::string>& paths) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		const std::vector<std::string_view> pathList = NonEmptyViewsOf(paths);

		if (pathList.empty())
		{
			return false;
		}

		Result<TreeHandle> fromTree = TreeOf(m_Handle.get(), from);

		if (!fromTree.HasValue())
		{
			return std::move(fromTree).GetError();
		}

		Result<TreeHandle> toTree = TreeOf(m_Handle.get(), to);

		if (!toTree.HasValue())
		{
			return std::move(toTree).GetError();
		}

		return DifferAt(m_Handle.get(), fromTree.GetValue().get(), toTree.GetValue().get(), pathList);
	}
}
