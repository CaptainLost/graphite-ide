#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>
#include <git2/sys/errors.h>

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		// Nothing for a merge, which carries no change of its own, and for a commit whose tree is its
		// first parent's. NotFound when the commit, or an object its patch needs, is missing.
		Result<std::optional<Oid>> ComputePatchId(git_repository* handle, const Oid& commitId)
		{
			Result<CommitHandle> looked = LookupCommit(handle, commitId);

			if (!looked.HasValue())
			{
				return std::move(looked).GetError();
			}

			const CommitHandle commit = std::move(looked).GetValue();

			if (git_commit_parentcount(commit.get()) > 1)
			{
				return std::optional<Oid>{};
			}

			Result<DiffHandle> diff = DiffWithFirstParent(handle, commit.get());

			if (!diff.HasValue())
			{
				return std::move(diff).GetError();
			}

			if (git_diff_num_deltas(diff.GetValue().get()) == 0)
			{
				return std::optional<Oid>{};
			}

			git_oid patchId{};
			git_error_clear();

			if (const int patchResult = git_diff_patchid(&patchId, diff.GetValue().get(), nullptr); patchResult != 0)
			{
				return ObjectReadFailure(patchResult, std::format("cannot work out the patch of {}", commitId.ShortHex()));
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

			Result<std::optional<Oid>> computed = ComputePatchId(handle, commit);

			if (computed.HasValue())
			{
				cache.Store(commit, computed.GetValue());
			}

			return computed;
		}

		bool IsMissingObjectFailure(const Error& patchFailure)
		{
			return patchFailure.Code == ErrorCode::NotFound;
		}

		struct OwnPatch
		{
			Oid Commit;
			Oid PatchId;
		};

		std::unordered_set<Oid> PatchIdsOf(const std::vector<OwnPatch>& ownPatches)
		{
			std::unordered_set<Oid> patchIds;

			for (const OwnPatch& ownPatch : ownPatches)
			{
				patchIds.insert(ownPatch.PatchId);
			}

			return patchIds;
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

		std::vector<OwnPatch> ownPatches;

		git_oid walked{};
		int nextResult = 0;

		while ((nextResult = git_revwalk_next(&walked, ownWalk.GetValue().get())) == 0)
		{
			Result<std::optional<Oid>> patchId = PatchIdOf(m_Handle.get(), ToOid(walked), cache);

			if (!patchId.HasValue())
			{
				if (!IsMissingObjectFailure(patchId.GetError()))
				{
					return std::move(patchId).GetError();
				}

				// WARNING: With one own patch unread, twins for all the others would still not make every own
				// change twinned, so the base side is not walked at all.
				match.OwnChanges = ownPatches.size();
				match.Complete = false;
				match.MissingObjects = true;
				return match;
			}

			if (patchId.GetValue().has_value())
			{
				ownPatches.push_back(OwnPatch{.Commit = ToOid(walked), .PatchId = *patchId.GetValue()});
			}
		}

		// WARNING: Only GIT_ITEROVER means the walk ran out. A walk that broke partway would
		// otherwise read as a branch with fewer commits of its own.
		if (nextResult != GIT_ITEROVER)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk the history: {}", LastLibgit2Message())};
		}

		match.OwnChanges = ownPatches.size();

		// A branch merged long ago has no change of its own, and its base side can hold every trunk commit
		// since: walking it would cost a patch id per commit for an answer already known.
		std::unordered_set<Oid> unmatched = PatchIdsOf(ownPatches);

		if (unmatched.empty())
		{
			return match;
		}

		Result<RevwalkHandle> baseWalk = StartWalk(m_Handle.get(), base, tip);

		if (!baseWalk.HasValue())
		{
			return std::move(baseWalk).GetError();
		}

		// The walk comes newest first, so the first base commit to carry a patch is the newest.
		std::unordered_map<Oid, Oid> twinOfPatchId;
		std::size_t read = 0;
		bool allMatched = false;

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
				if (!IsMissingObjectFailure(patchId.GetError()))
				{
					return std::move(patchId).GetError();
				}

				match.Complete = false;
				match.MissingObjects = true;
				break;
			}

			if (patchId.GetValue().has_value() && unmatched.erase(*patchId.GetValue()) == 1)
			{
				twinOfPatchId.emplace(*patchId.GetValue(), ToOid(walked));
			}

			if (unmatched.empty())
			{
				allMatched = true;
				break;
			}
		}

		if (match.Complete && !allMatched && nextResult != GIT_ITEROVER)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk the history: {}", LastLibgit2Message())};
		}

		for (const OwnPatch& ownPatch : ownPatches)
		{
			if (twinOfPatchId.contains(ownPatch.PatchId))
			{
				++match.InBase;
				match.Twins.push_back(PatchTwin{.Own = ownPatch.Commit, .Twin = twinOfPatchId.at(ownPatch.PatchId)});
			}
		}

		return match;
	}

	Result<std::optional<Oid>> Repository::FindPatchTwin(const Oid& commit, const Oid& lineTip, std::size_t budget,
														 PatchIdCache& cache) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		if (Result<CommitHandle> lineCommit = LookupCommit(m_Handle.get(), lineTip); !lineCommit.HasValue())
		{
			return std::move(lineCommit).GetError();
		}

		Result<std::optional<Oid>> ownPatchId = PatchIdOf(m_Handle.get(), commit, cache);

		if (!ownPatchId.HasValue())
		{
			return std::move(ownPatchId).GetError();
		}

		const std::optional<Oid> noTwin;

		if (!ownPatchId.GetValue().has_value())
		{
			return noTwin;
		}

		const Oid wantedPatchId = *ownPatchId.GetValue();

		Result<RevwalkHandle> lineWalk = StartWalk(m_Handle.get(), lineTip, commit);

		if (!lineWalk.HasValue())
		{
			return std::move(lineWalk).GetError();
		}

		git_oid walked{};
		int nextResult = 0;
		std::size_t read = 0;

		while ((nextResult = git_revwalk_next(&walked, lineWalk.GetValue().get())) == 0)
		{
			if (read == budget)
			{
				return noTwin;
			}

			++read;

			Result<std::optional<Oid>> patchId = PatchIdOf(m_Handle.get(), ToOid(walked), cache);

			if (!patchId.HasValue())
			{
				return std::move(patchId).GetError();
			}

			const bool carriesTheSamePatch = patchId.GetValue() == wantedPatchId;

			if (carriesTheSamePatch)
			{
				return std::optional<Oid>{ToOid(walked)};
			}
		}

		if (nextResult != GIT_ITEROVER)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot walk the history: {}", LastLibgit2Message())};
		}

		return noTwin;
	}
}
