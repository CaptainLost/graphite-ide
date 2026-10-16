#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	namespace {

		constexpr std::string_view kLocalBranchPrefix = "refs/heads/";

		constexpr int kHeadIsDetached = 1;

		RepositoryOperation ToRepositoryOperation(int repositoryState)
		{
			switch (repositoryState)
			{
			case GIT_REPOSITORY_STATE_MERGE:
				return RepositoryOperation::Merge;

			case GIT_REPOSITORY_STATE_REVERT:
			case GIT_REPOSITORY_STATE_REVERT_SEQUENCE:
				return RepositoryOperation::Revert;

			case GIT_REPOSITORY_STATE_CHERRYPICK:
			case GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE:
				return RepositoryOperation::CherryPick;

			case GIT_REPOSITORY_STATE_BISECT:
				return RepositoryOperation::Bisect;

			case GIT_REPOSITORY_STATE_REBASE:
			case GIT_REPOSITORY_STATE_REBASE_INTERACTIVE:
			case GIT_REPOSITORY_STATE_REBASE_MERGE:
			case GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE:
				return RepositoryOperation::Rebase;

			case GIT_REPOSITORY_STATE_APPLY_MAILBOX:
				return RepositoryOperation::ApplyMailbox;

			default:
				return RepositoryOperation::None;
			}
		}

		// An unborn HEAD is only a symbolic name, so there is no branch object to ask
		// git_branch_name about.
		std::string ShortBranchName(std::string_view referenceName)
		{
			if (referenceName.starts_with(kLocalBranchPrefix))
			{
				referenceName.remove_prefix(kLocalBranchPrefix.size());
			}

			return std::string{referenceName};
		}

		std::optional<std::string> BranchNameOf(const git_reference* reference)
		{
			const char* name = nullptr;

			if (git_branch_name(&name, reference) != 0 || name == nullptr)
			{
				return std::nullopt;
			}

			return std::string{name};
		}

		std::optional<CommitSummary> ReadTipCommit(git_repository* handle, const git_oid& tipId)
		{
			CommitHandle commit;

			if (git_commit_lookup(Out(commit), handle, &tipId) != 0)
			{
				return std::nullopt;
			}

			return ToCommitSummary(commit.get());
		}

		// WARNING: git_branch_is_head re-reads HEAD from disk on every call, which dominates a listing
		// of thousands of branches, so HEAD is resolved once and compared by reference name.
		std::optional<std::string> ResolveHeadReferenceName(git_repository* handle)
		{
			ReferenceHandle headReference;

			const int headResult = git_repository_head(Out(headReference), handle);

			if (headResult != 0 || headReference == nullptr)
			{
				return std::nullopt;
			}

			const char* headName = git_reference_name(headReference.get());

			if (headName == nullptr)
			{
				return std::nullopt;
			}

			return std::string{headName};
		}

		bool IsHeadBranch(const Branch& branch, const std::optional<std::string>& headReferenceName)
		{
			return branch.Kind == RefKind::Local && headReferenceName.has_value() && branch.FullName == *headReferenceName;
		}

		enum class ConfigEntryState
		{
			Set,
			Missing,
			Unknown
		};

		ConfigEntryState ReadConfigEntryState(const git_config* snapshot, const std::string& key)
		{
			const char* value = nullptr;
			const int lookupResult = git_config_get_string(&value, snapshot, key.c_str());

			if (lookupResult == GIT_ENOTFOUND)
			{
				return ConfigEntryState::Missing;
			}

			if (lookupResult != 0)
			{
				return ConfigEntryState::Unknown;
			}

			return (value != nullptr && value[0] != '\0') ? ConfigEntryState::Set : ConfigEntryState::Missing;
		}

		// WARNING: git_branch_upstream takes a new configuration snapshot per call and fails for every
		// branch without an upstream, so the listing's one snapshot is asked first. An entry whose
		// lookup fails for any reason but its absence counts as set: git_branch_upstream then decides,
		// unless the other entry is missing, when it would fail anyway.
		bool MayHaveUpstream(const git_config* snapshot, const Branch& branch)
		{
			if (snapshot == nullptr)
			{
				return true;
			}

			const std::string shortName = ShortBranchName(branch.FullName);
			const ConfigEntryState remoteState = ReadConfigEntryState(snapshot, "branch." + shortName + ".remote");
			const ConfigEntryState mergeState = ReadConfigEntryState(snapshot, "branch." + shortName + ".merge");

			if (remoteState == ConfigEntryState::Missing || mergeState == ConfigEntryState::Missing)
			{
				return false;
			}

			return true;
		}

		bool ComesBeforeInBranchList(const Branch& branch, const Branch& otherBranch)
		{
			if (branch.Kind != otherBranch.Kind)
			{
				return branch.Kind == RefKind::Local;
			}

			return branch.Name < otherBranch.Name;
		}
	}

	Result<std::vector<Branch>> Repository::QueryBranches() const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		BranchIteratorHandle branchIterator;

		if (git_branch_iterator_new(Out(branchIterator), m_Handle.get(), GIT_BRANCH_ALL) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot list the branches: {}", LastLibgit2Message())};
		}

		std::vector<Branch> branches;
		std::vector<std::string> remoteHeadTargets;

		const std::optional<std::string> headReferenceName = ResolveHeadReferenceName(m_Handle.get());

		// WARNING: A snapshot that cannot be taken leaves the handle null, and every local branch then
		// asks git_branch_upstream itself.
		ConfigHandle configSnapshot;
		git_repository_config_snapshot(Out(configSnapshot), m_Handle.get());

		git_branch_t branchType = GIT_BRANCH_LOCAL;

		int nextResult = 0;

		while (true)
		{
			ReferenceHandle reference;
			nextResult = git_branch_next(Out(reference), &branchType, branchIterator.get());

			if (nextResult != 0)
			{
				break;
			}

			Branch branch;
			branch.Kind = branchType == GIT_BRANCH_REMOTE ? RefKind::Remote : RefKind::Local;

			if (const std::optional<std::string> name = BranchNameOf(reference.get()); name.has_value())
			{
				branch.Name = *name;
			}

			if (const char* fullName = git_reference_name(reference.get()); fullName != nullptr)
			{
				branch.FullName = fullName;
			}

			// A symbolic ref such as refs/remotes/origin/HEAD has no direct target: it names another
			// branch rather than a commit.
			const git_oid* tipId = git_reference_target(reference.get());

			if (tipId == nullptr)
			{
				const char* symbolicTarget = git_reference_symbolic_target(reference.get());

				if (branch.Kind == RefKind::Remote && symbolicTarget != nullptr)
				{
					remoteHeadTargets.emplace_back(symbolicTarget);
				}

				continue;
			}

			branch.Tip = ToOid(*tipId);
			branch.IsHead = IsHeadBranch(branch, headReferenceName);
			branch.TipCommit = ReadTipCommit(m_Handle.get(), *tipId);

			ReferenceHandle upstream;

			const bool askUpstream = branch.Kind == RefKind::Remote || MayHaveUpstream(configSnapshot.get(), branch);
			const int upstreamResult = askUpstream ? git_branch_upstream(Out(upstream), reference.get()) : GIT_ENOTFOUND;

			if (upstreamResult == 0 && upstream != nullptr)
			{
				branch.Upstream = BranchNameOf(upstream.get());

				if (const git_oid* upstreamTipId = git_reference_target(upstream.get()); upstreamTipId != nullptr)
				{
					std::size_t ahead = 0;
					std::size_t behind = 0;

					if (git_graph_ahead_behind(&ahead, &behind, m_Handle.get(), tipId, upstreamTipId) == 0)
					{
						branch.Ahead = ahead;
						branch.Behind = behind;
						branch.DistanceKnown = true;
					}
				}
			}

			branches.push_back(std::move(branch));
		}

		// Anything but GIT_ITEROVER means the listing stopped partway, and a partial list must not
		// pass for the whole one.
		if (nextResult != GIT_ITEROVER)
		{
			const std::string message = LastLibgit2Message();
			return Error{ErrorCode::IoFailure, std::format("cannot list the branches: {}", message)};
		}

		// Marked only once the listing is complete: origin/HEAD sorts before the branch it names.
		for (Branch& branch : branches)
		{
			branch.IsRemoteDefault = std::ranges::find(remoteHeadTargets, branch.FullName) != remoteHeadTargets.end();
		}

		std::sort(branches.begin(), branches.end(), ComesBeforeInBranchList);

		return branches;
	}

	Result<HeadState> Repository::QueryHead() const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		HeadState head;
		head.Operation = ToRepositoryOperation(git_repository_state(m_Handle.get()));

		const int unbornResult = git_repository_head_unborn(m_Handle.get());

		if (unbornResult < 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		if (unbornResult == kHeadIsUnborn)
		{
			head.Kind = HeadKind::Unborn;

			ReferenceHandle symbolicHead;

			if (git_reference_lookup(Out(symbolicHead), m_Handle.get(), "HEAD") == 0)
			{
				if (const char* symbolicTarget = git_reference_symbolic_target(symbolicHead.get());
					symbolicTarget != nullptr)
				{
					head.BranchName = ShortBranchName(symbolicTarget);
				}
			}

			return head;
		}

		ReferenceHandle headReference;

		if (git_repository_head(Out(headReference), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		if (const git_oid* headCommitId = git_reference_target(headReference.get()); headCommitId != nullptr)
		{
			head.Commit = ToOid(*headCommitId);
		}

		if (git_repository_head_detached(m_Handle.get()) == kHeadIsDetached)
		{
			head.Kind = HeadKind::Detached;
		}
		else
		{
			head.Kind = HeadKind::Branch;
			head.BranchName = git_reference_shorthand(headReference.get());
		}

		return head;
	}
}
