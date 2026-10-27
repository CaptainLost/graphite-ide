#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		// Nothing for a merge, which carries no change of its own, and for a commit whose tree is its
		// first parent's. Outer empty only when libgit2 failed.
		std::optional<std::optional<Oid>> ComputePatchId(git_repository* handle, const Oid& commitId)
		{
			Result<CommitHandle> looked = LookupCommit(handle, commitId);

			if (!looked.HasValue())
			{
				return std::nullopt;
			}

			const CommitHandle commit = std::move(looked).GetValue();

			if (git_commit_parentcount(commit.get()) > 1)
			{
				return std::optional<Oid>{};
			}

			TreeHandle tree;

			if (git_commit_tree(Out(tree), commit.get()) != 0)
			{
				return std::nullopt;
			}

			// A root commit is diffed against the empty tree, which libgit2 takes as a null one.
			TreeHandle parentTree;

			if (git_commit_parentcount(commit.get()) == 1)
			{
				CommitHandle parent;

				if (git_commit_parent(Out(parent), commit.get(), kFirstParentIndex) != 0)
				{
					return std::nullopt;
				}

				if (git_commit_tree(Out(parentTree), parent.get()) != 0)
				{
					return std::nullopt;
				}
			}

			DiffHandle diff;

			if (git_diff_tree_to_tree(Out(diff), handle, parentTree.get(), tree.get(), nullptr) != 0)
			{
				return std::nullopt;
			}

			if (git_diff_num_deltas(diff.get()) == 0)
			{
				return std::optional<Oid>{};
			}

			git_oid patchId{};

			if (git_diff_patchid(&patchId, diff.get(), nullptr) != 0)
			{
				return std::nullopt;
			}

			return std::optional<Oid>{ToOid(patchId)};
		}

		// The cache is read and written around the computation, not held across it: two workers may
		// compute the same id, and both arrive at the same answer.
		Result<std::optional<Oid>> PatchIdOf(git_repository* handle, const Oid& commit, PatchIdCache& cache)
		{
			if (std::optional<std::optional<Oid>> known = cache.Find(commit))
			{
				return *known;
			}

			std::optional<std::optional<Oid>> computed = ComputePatchId(handle, commit);

			if (!computed)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot work out the patch of {}: {}", commit.ShortHex(), LastLibgit2Message())};
			}

			cache.Store(commit, *computed);
			return *computed;
		}

		Result<RevwalkHandle> StartWalk(git_repository* handle, const Oid& include, const Oid& exclude)
		{
			RevwalkHandle walk;

			if (git_revwalk_new(Out(walk), handle) != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot walk the history: {}", LastLibgit2Message())};
			}

			const git_oid includeId = ToGitOid(include);
			const git_oid excludeId = ToGitOid(exclude);

			if (git_revwalk_push(walk.get(), &includeId) != 0 || git_revwalk_hide(walk.get(), &excludeId) != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot walk the history: {}", LastLibgit2Message())};
			}

			return walk;
		}
	}

	Result<PatchMatch> Repository::MatchPatches(const Oid& tip, const Oid& base, std::size_t budget,
												PatchIdCache& cache) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		if (Result<CommitHandle> tipCommit = LookupCommit(m_Handle.get(), tip); !tipCommit.HasValue())
		{
			return std::move(tipCommit).GetError();
		}

		if (Result<CommitHandle> baseCommit = LookupCommit(m_Handle.get(), base); !baseCommit.HasValue())
		{
			return std::move(baseCommit).GetError();
		}

		const git_oid tipId = ToGitOid(tip);
		const git_oid baseId = ToGitOid(base);

		PatchMatch match;

		if (git_graph_ahead_behind(&match.Ahead, &match.Behind, m_Handle.get(), &tipId, &baseId) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot count the commits between {} and {}: {}",
														   tip.ShortHex(), base.ShortHex(), LastLibgit2Message())};
		}

		Result<RevwalkHandle> ownWalk = StartWalk(m_Handle.get(), tip, base);

		if (!ownWalk.HasValue())
		{
			return std::move(ownWalk).GetError();
		}

		std::vector<Oid> ownPatchIds;

		git_oid walked{};
		int nextResult = 0;

		while ((nextResult = git_revwalk_next(&walked, ownWalk.GetValue().get())) == 0)
		{
			Result<std::optional<Oid>> patchId = PatchIdOf(m_Handle.get(), ToOid(walked), cache);

			if (!patchId.HasValue())
			{
				return std::move(patchId).GetError();
			}

			if (patchId.GetValue().has_value())
			{
				ownPatchIds.push_back(*patchId.GetValue());
			}
		}

		// WARNING: Only GIT_ITEROVER means the walk ran out. A walk that broke partway would
		// otherwise read as a branch with fewer commits of its own.
		if (nextResult != GIT_ITEROVER)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk the history: {}", LastLibgit2Message())};
		}

		Result<RevwalkHandle> baseWalk = StartWalk(m_Handle.get(), base, tip);

		if (!baseWalk.HasValue())
		{
			return std::move(baseWalk).GetError();
		}

		std::unordered_set<Oid> basePatches;
		std::size_t read = 0;

		while ((nextResult = git_revwalk_next(&walked, baseWalk.GetValue().get())) == 0)
		{
			if (read == budget)
			{
				match.Complete = false;
				break;
			}

			++read;

			Result<std::optional<Oid>> patchId = PatchIdOf(m_Handle.get(), ToOid(walked), cache);

			if (!patchId.HasValue())
			{
				return std::move(patchId).GetError();
			}

			if (patchId.GetValue().has_value())
			{
				basePatches.insert(*patchId.GetValue());
			}
		}

		if (match.Complete && nextResult != GIT_ITEROVER)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk the history: {}", LastLibgit2Message())};
		}

		match.OwnChanges = ownPatchIds.size();

		for (const Oid& patchId : ownPatchIds)
		{
			if (basePatches.contains(patchId))
			{
				++match.InBase;
			}
		}

		return match;
	}
}
