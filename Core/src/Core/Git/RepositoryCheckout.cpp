#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace Core {

	namespace {

		constexpr const char* kRemoteBranchPrefix = "refs/remotes/";
		constexpr const char* kPeelToCommit = "^{commit}";
		constexpr char kRemoteNameSeparator = '/';

		constexpr int kContinueTheCheckout = 0;

		// The files `git reset --hard` removes: the state of a merge, a cherry-pick or a revert that
		// waits for a commit it no longer has anything for. A rebase, a sequence of picks and a bisect
		// go on across the reset, so their files stay.
		constexpr const char* kPendingCommitStateFiles[] = {"MERGE_HEAD", "MERGE_MSG", "MERGE_MODE",
															"MERGE_RR", "AUTO_MERGE", "SQUASH_MSG",
															"CHERRY_PICK_HEAD", "REVERT_HEAD"};

		const char* const kResolveConflictsFirstMessage =
			"the index has unresolved conflicts; resolve or discard the conflicts first";

		// libgit2 converts between git_object and a concrete object type with a plain pointer cast by
		// design; the object must already have been peeled to that type.
		git_commit* AsCommit(git_object* peeledCommit)
		{
			return reinterpret_cast<git_commit*>(peeledCommit);
		}

		git_tree* AsTree(git_object* peeledTree)
		{
			return reinterpret_cast<git_tree*>(peeledTree);
		}

		git_object* AsObject(git_tree* tree)
		{
			return reinterpret_cast<git_object*>(tree);
		}

		std::string LocalNameOf(const std::string& remoteBranchName)
		{
			const std::size_t separatorPosition = remoteBranchName.find(kRemoteNameSeparator);

			if (separatorPosition == std::string::npos)
			{
				return remoteBranchName;
			}

			return remoteBranchName.substr(separatorPosition + 1);
		}

		// Only "no such branch" is false: a lookup that failed for another reason is not evidence the
		// branch is absent, and reading it so would create a second one.
		Result<bool> LocalBranchExists(git_repository* handle, const std::string& name)
		{
			ReferenceHandle branch;
			const int lookupResult = git_branch_lookup(Out(branch), handle, name.c_str(), GIT_BRANCH_LOCAL);

			if (lookupResult == GIT_ENOTFOUND)
			{
				return false;
			}

			if (lookupResult != 0)
			{
				return Error{ErrorCode::InvalidArgument,
							 std::format("cannot look up branch '{}': {}", name, LastLibgit2Message())};
			}

			return true;
		}

		struct ResolvedTarget
		{
			ObjectHandle Commit;
			std::string ReferenceForHead;
			bool MustCreateTrackingBranch = false;
			std::string LocalBranchName;
		};

		Result<ResolvedTarget> ResolveCheckoutTarget(git_repository* handle, const CheckoutTarget& target)
		{
			ResolvedTarget resolved;
			std::string revisionSpec;

			switch (target.Kind)
			{
			case CheckoutKind::LocalBranch:
				revisionSpec = LocalBranchReference(target.Name);
				resolved.ReferenceForHead = revisionSpec;
				break;

			case CheckoutKind::RemoteBranch:
			{
				resolved.LocalBranchName = LocalNameOf(target.Name);

				const Result<bool> localBranchExists = LocalBranchExists(handle, resolved.LocalBranchName);

				if (!localBranchExists.HasValue())
				{
					return localBranchExists.GetError();
				}

				if (localBranchExists.GetValue())
				{
					revisionSpec = LocalBranchReference(resolved.LocalBranchName);
				}
				else
				{
					revisionSpec = kRemoteBranchPrefix + target.Name;
					resolved.MustCreateTrackingBranch = true;
				}

				resolved.ReferenceForHead = LocalBranchReference(resolved.LocalBranchName);
				break;
			}

			case CheckoutKind::Commit:
				revisionSpec = target.Commit.ToHex();
				break;
			}

			const int resolveResult =
				git_revparse_single(Out(resolved.Commit), handle, (revisionSpec + kPeelToCommit).c_str());

			if (resolveResult != 0)
			{
				const bool targetNotFound = resolveResult == GIT_ENOTFOUND;
				const ErrorCode errorCode = targetNotFound ? ErrorCode::NotFound : ErrorCode::InvalidArgument;

				return Error{errorCode, std::format("cannot resolve '{}': {}", revisionSpec, LastLibgit2Message())};
			}

			return resolved;
		}

		// Empty with no HEAD yet, which has no tree to compare.
		Result<DiffHandle> DiffFromHeadToTarget(git_repository* handle, git_object* targetCommit)
		{
			DiffHandle diff;
			const int unbornResult = git_repository_head_unborn(handle);

			if (unbornResult < 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot read HEAD: {}", LastLibgit2Message())};
			}

			if (unbornResult == kHeadIsUnborn)
			{
				return diff;
			}

			ObjectHandle headTree;

			if (git_revparse_single(Out(headTree), handle, "HEAD^{tree}") != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot read HEAD's tree: {}", LastLibgit2Message())};
			}

			TreeHandle targetTree;

			if (git_commit_tree(Out(targetTree), AsCommit(targetCommit)) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read the target's tree: {}", LastLibgit2Message())};
			}

			const git_diff_options* defaultDiffOptions = nullptr;
			const int diffResult =
				git_diff_tree_to_tree(Out(diff), handle, AsTree(headTree.get()), targetTree.get(), defaultDiffOptions);

			if (diffResult != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot compare the trees: {}", LastLibgit2Message())};
			}

			return diff;
		}

		std::size_t DeltaCount(const git_diff* diff)
		{
			if (diff == nullptr)
			{
				return 0;
			}

			return git_diff_num_deltas(diff);
		}

		// libgit2 notifies a removal only when it conflicts and queues a clean one silently, so the list
		// is read from the two trees the checkout diffs. It must be read before the checkout, which
		// would leave nothing on disk to see.
		Result<std::vector<std::filesystem::path>> ListFilesCheckoutWouldRemove(
			git_repository* handle, git_object* targetCommit, const std::filesystem::path& workingDirectory)
		{
			Result<DiffHandle> headToTarget = DiffFromHeadToTarget(handle, targetCommit);

			if (!headToTarget.HasValue())
			{
				return headToTarget.GetError();
			}

			std::vector<std::filesystem::path> filesToRemove;
			const git_diff* diff = headToTarget.GetValue().get();
			const std::size_t deltaCount = DeltaCount(diff);

			for (std::size_t deltaIndex = 0; deltaIndex < deltaCount; ++deltaIndex)
			{
				const git_diff_delta* delta = git_diff_get_delta(diff, deltaIndex);

				if (delta->status != GIT_DELTA_DELETED)
				{
					continue;
				}

				std::filesystem::path relativePath = FromUtf8(delta->old_file.path);
				std::error_code existsError;

				if (std::filesystem::exists(workingDirectory / relativePath, existsError))
				{
					filesToRemove.push_back(std::move(relativePath));
				}
			}

			return filesToRemove;
		}

		// With unresolved conflicts libgit2 refuses even a dry run before it looks at a single file, so
		// this is the plan's best word on what the checkout will write once the conflicts are cleared:
		// discarding or stashing them leaves the tree at HEAD, so HEAD is where the checkout starts.
		Result<std::vector<std::filesystem::path>> ListPathsDifferingFromHead(git_repository* handle,
																			  git_object* targetCommit)
		{
			Result<DiffHandle> headToTarget = DiffFromHeadToTarget(handle, targetCommit);

			if (!headToTarget.HasValue())
			{
				return headToTarget.GetError();
			}

			std::vector<std::filesystem::path> differingPaths;
			const git_diff* diff = headToTarget.GetValue().get();
			const std::size_t deltaCount = DeltaCount(diff);

			for (std::size_t deltaIndex = 0; deltaIndex < deltaCount; ++deltaIndex)
			{
				const git_diff_delta* delta = git_diff_get_delta(diff, deltaIndex);
				const bool isDeleted = delta->status == GIT_DELTA_DELETED;
				const char* path = isDeleted ? delta->old_file.path : delta->new_file.path;
				differingPaths.push_back(FromUtf8(path));
			}

			return differingPaths;
		}

		// The index is re-read first, because a merge or a pop that conflicted since it was last read is
		// exactly what this looks for.
		Result<std::vector<std::filesystem::path>> ListConflictedPaths(git_repository* handle)
		{
			Result<IndexHandle> openedIndex = OpenFreshIndex(handle);

			if (!openedIndex.HasValue())
			{
				return openedIndex.GetError();
			}

			const IndexHandle index = std::move(openedIndex).GetValue();
			std::vector<std::filesystem::path> conflictedPaths;

			if (!git_index_has_conflicts(index.get()))
			{
				return conflictedPaths;
			}

			// The entries are sorted by path and then stage, so a conflict's two or three stages sit
			// next to each other.
			const std::size_t entryCount = git_index_entrycount(index.get());
			const char* previousConflictedPath = nullptr;

			for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
			{
				const git_index_entry* entry = git_index_get_byindex(index.get(), entryIndex);

				if (entry == nullptr || GIT_INDEX_ENTRY_STAGE(entry) == GIT_INDEX_STAGE_NORMAL)
				{
					continue;
				}

				const bool isNewPath =
					previousConflictedPath == nullptr || std::strcmp(previousConflictedPath, entry->path) != 0;

				if (isNewPath)
				{
					conflictedPaths.push_back(FromUtf8(entry->path));
				}

				previousConflictedPath = entry->path;
			}

			return conflictedPaths;
		}

		struct NotifiedPaths
		{
			std::vector<std::filesystem::path> Overwritten;
			std::vector<std::filesystem::path> Updated;
		};

		// SAFE refuses over CONFLICT: a tracked file changed on disk that the target changes or removes,
		// or an untracked file where the target has one. Removals never come through here.
		int CollectNotifiedPath(git_checkout_notify_t notification, const char* path, const git_diff_file*,
								const git_diff_file*, const git_diff_file*, void* payload)
		{
			NotifiedPaths& notified = *static_cast<NotifiedPaths*>(payload);

			if (path == nullptr)
			{
				return kContinueTheCheckout;
			}

			if (notification == GIT_CHECKOUT_NOTIFY_CONFLICT)
			{
				notified.Overwritten.push_back(FromUtf8(path));
			}
			else if (notification == GIT_CHECKOUT_NOTIFY_UPDATED)
			{
				notified.Updated.push_back(FromUtf8(path));
			}

			return kContinueTheCheckout;
		}

		enum class CheckoutRun
		{
			DryRun,
			Write
		};

		git_checkout_options MakeSafeCheckoutOptions(NotifiedPaths& notified, CheckoutRun run)
		{
			unsigned int strategy = static_cast<unsigned int>(GIT_CHECKOUT_SAFE);

			if (run == CheckoutRun::DryRun)
			{
				strategy |= static_cast<unsigned int>(GIT_CHECKOUT_DRY_RUN);
			}

			git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
			options.checkout_strategy = strategy;
			options.notify_flags = GIT_CHECKOUT_NOTIFY_CONFLICT | GIT_CHECKOUT_NOTIFY_UPDATED;
			options.notify_cb = CollectNotifiedPath;
			options.notify_payload = &notified;
			return options;
		}

		// Not libgit2's REMOVE_UNTRACKED, which deletes an untracked directory with everything in it,
		// ignored files included. The status list is built without INCLUDE_IGNORED, so ignored files
		// never reach it.
		Result<void> RemoveUntracked(git_repository* handle, const std::filesystem::path& workingDirectory)
		{
			git_status_options options = GIT_STATUS_OPTIONS_INIT;
			options.show = GIT_STATUS_SHOW_WORKDIR_ONLY;
			options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

			StatusListHandle statusList;

			if (git_status_list_new(Out(statusList), handle, &options) != 0)
			{
				return Error{ErrorCode::IoFailure, std::format("cannot list untracked files: {}", LastLibgit2Message())};
			}

			std::vector<std::filesystem::path> untrackedFiles;
			const std::size_t entryCount = git_status_list_entrycount(statusList.get());

			for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
			{
				const git_status_entry* entry = git_status_byindex(statusList.get(), entryIndex);
				const bool isUntracked = (entry->status & GIT_STATUS_WT_NEW) != 0;

				if (isUntracked && entry->index_to_workdir != nullptr)
				{
					untrackedFiles.push_back(FromUtf8(entry->index_to_workdir->old_file.path));
				}
			}

			return RemoveUntrackedFiles(workingDirectory, untrackedFiles);
		}

		Result<void> EndPendingCommit(git_repository* handle)
		{
			const std::filesystem::path gitDirectory = FromUtf8(git_repository_path(handle));

			for (const char* stateFile : kPendingCommitStateFiles)
			{
				std::error_code removeError;
				std::filesystem::remove(gitDirectory / stateFile, removeError);

				if (removeError)
				{
					return Error{ErrorCode::IoFailure,
								 std::format("the changes were discarded but '{}' could not be removed: {}", stateFile,
											 removeError.message())};
				}
			}

			return Result<void>::Ok();
		}

		bool Contains(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& wanted)
		{
			for (const std::filesystem::path& path : paths)
			{
				if (path == wanted)
				{
					return true;
				}
			}

			return false;
		}

		void AddUnlessOverwritten(std::vector<std::filesystem::path>& pathsTheCheckoutWrites,
								  std::vector<std::filesystem::path> candidatePaths,
								  const std::vector<std::filesystem::path>& overwrittenUserWork)
		{
			for (std::filesystem::path& candidatePath : candidatePaths)
			{
				if (!Contains(overwrittenUserWork, candidatePath))
				{
					pathsTheCheckoutWrites.push_back(std::move(candidatePath));
				}
			}
		}

		CheckoutOutcome FailedWithNothingChanged(Error error)
		{
			return CheckoutOutcome{.Failure = std::move(error)};
		}
	}

	Result<CheckoutPlan> Repository::PlanCheckout(const CheckoutTarget& target) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		Result<ResolvedTarget> resolved = ResolveCheckoutTarget(m_Handle.get(), target);

		if (!resolved.HasValue())
		{
			return resolved.GetError();
		}

		git_object* targetCommit = resolved.GetValue().Commit.get();
		Result<std::vector<std::filesystem::path>> conflictedPaths = ListConflictedPaths(m_Handle.get());

		if (!conflictedPaths.HasValue())
		{
			return conflictedPaths.GetError();
		}

		// libgit2 refuses a checkout, a dry run too, over unresolved conflicts without naming a file,
		// which would read as a plan with nothing in the way. Listed as overwritten, the conflicts open
		// the question, and Discard or Stash is what clears them.
		if (!conflictedPaths.GetValue().empty())
		{
			Result<std::vector<std::filesystem::path>> differingPaths =
				ListPathsDifferingFromHead(m_Handle.get(), targetCommit);

			if (!differingPaths.HasValue())
			{
				return differingPaths.GetError();
			}

			CheckoutPlan plan{.Overwritten = std::move(conflictedPaths).GetValue()};
			AddUnlessOverwritten(plan.Updated, std::move(differingPaths).GetValue(), plan.Overwritten);
			return plan;
		}

		Result<std::vector<std::filesystem::path>> filesToRemove =
			ListFilesCheckoutWouldRemove(m_Handle.get(), targetCommit, m_WorkingDirectory);

		if (!filesToRemove.HasValue())
		{
			return filesToRemove.GetError();
		}

		NotifiedPaths notified;
		git_checkout_options options = MakeSafeCheckoutOptions(notified, CheckoutRun::DryRun);

		// A dry run that finds conflicts answers GIT_ECONFLICT, having notified every one of them first.
		const int planResult = git_checkout_tree(m_Handle.get(), targetCommit, &options);
		const bool foundConflicts = planResult == GIT_ECONFLICT;

		if (planResult != 0 && !foundConflicts)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot plan the checkout: {}", LastLibgit2Message())};
		}

		AddUnlessOverwritten(notified.Updated, std::move(filesToRemove).GetValue(), notified.Overwritten);
		return CheckoutPlan{.Overwritten = std::move(notified.Overwritten), .Updated = std::move(notified.Updated)};
	}

	CheckoutOutcome Repository::Checkout(const CheckoutTarget& target)
	{
		if (m_Handle == nullptr)
		{
			return FailedWithNothingChanged(Error{ErrorCode::NotFound, "the repository is not open"});
		}

		Result<ResolvedTarget> resolved = ResolveCheckoutTarget(m_Handle.get(), target);

		if (!resolved.HasValue())
		{
			return FailedWithNothingChanged(resolved.GetError());
		}

		const ResolvedTarget& resolvedTarget = resolved.GetValue();
		Result<std::vector<std::filesystem::path>> conflictedPaths = ListConflictedPaths(m_Handle.get());

		if (!conflictedPaths.HasValue())
		{
			return FailedWithNothingChanged(conflictedPaths.GetError());
		}

		// Refused here, in words that say why, because libgit2's own refusal looks like any other
		// "would overwrite changes" one.
		if (!conflictedPaths.GetValue().empty())
		{
			return FailedWithNothingChanged(Error{ErrorCode::InvalidArgument, kResolveConflictsFirstMessage});
		}

		Result<std::vector<std::filesystem::path>> filesToRemove =
			ListFilesCheckoutWouldRemove(m_Handle.get(), resolvedTarget.Commit.get(), m_WorkingDirectory);

		if (!filesToRemove.HasValue())
		{
			return FailedWithNothingChanged(filesToRemove.GetError());
		}

		// The tracking branch is created before the files move, so that a failure to create it stops
		// the checkout before anything changes; a failed checkout deletes it again.
		ReferenceHandle createdTrackingBranch;

		if (resolvedTarget.MustCreateTrackingBranch)
		{
			const int failIfTheBranchExists = 0;

			if (git_branch_create(Out(createdTrackingBranch), m_Handle.get(), resolvedTarget.LocalBranchName.c_str(),
								  AsCommit(resolvedTarget.Commit.get()), failIfTheBranchExists) != 0)
			{
				return FailedWithNothingChanged(
					Error{ErrorCode::InvalidArgument, std::format("cannot create branch '{}': {}",
																  resolvedTarget.LocalBranchName, LastLibgit2Message())});
			}

			if (git_branch_set_upstream(createdTrackingBranch.get(), target.Name.c_str()) != 0)
			{
				std::string message = std::format("cannot make '{}' track '{}': {}", resolvedTarget.LocalBranchName,
												  target.Name, LastLibgit2Message());

				if (git_branch_delete(createdTrackingBranch.get()) != 0)
				{
					message += std::format("; the new branch could not be removed: {}", LastLibgit2Message());
				}

				return FailedWithNothingChanged(Error{ErrorCode::IoFailure, std::move(message)});
			}
		}

		const bool trackingBranchWasCreated = createdTrackingBranch != nullptr;

		NotifiedPaths notified;
		git_checkout_options options = MakeSafeCheckoutOptions(notified, CheckoutRun::Write);

		// SAFE works out and notifies every action before it writes anything, so a conflict is refused
		// with the working tree and the index exactly as they were, and by the time any other failure
		// can happen, Updated already holds everything the checkout meant to write.
		const int checkoutResult = git_checkout_tree(m_Handle.get(), resolvedTarget.Commit.get(), &options);
		AddUnlessOverwritten(notified.Updated, std::move(filesToRemove).GetValue(), notified.Overwritten);

		if (checkoutResult != 0)
		{
			const bool refusedOverChanges = checkoutResult == GIT_ECONFLICT;
			std::string message;

			if (refusedOverChanges)
			{
				message = std::format("checking out would overwrite changes: {}", LastLibgit2Message());
			}
			else
			{
				message = std::format("the checkout failed partway and the working tree may be partly "
									  "switched; HEAD has not moved: {}",
									  LastLibgit2Message());
			}

			if (trackingBranchWasCreated)
			{
				if (git_branch_delete(createdTrackingBranch.get()) != 0)
				{
					message += std::format("; the new branch could not be removed: {}", LastLibgit2Message());
				}
			}

			if (refusedOverChanges)
			{
				return FailedWithNothingChanged(Error{ErrorCode::InvalidArgument, std::move(message)});
			}

			// Any failure other than a refusal may have written or removed any of the planned paths, so
			// all of them are named.
			return CheckoutOutcome{.Changed = std::move(notified.Updated),
								   .Failure = Error{ErrorCode::IoFailure, std::move(message)}};
		}

		int setHeadResult = 0;

		if (target.Kind == CheckoutKind::Commit)
		{
			setHeadResult = git_repository_set_head_detached(m_Handle.get(), git_object_id(resolvedTarget.Commit.get()));
		}
		else
		{
			setHeadResult = git_repository_set_head(m_Handle.get(), resolvedTarget.ReferenceForHead.c_str());
		}

		if (setHeadResult != 0)
		{
			std::string message =
				std::format("the files were checked out but HEAD could not be moved: {}", LastLibgit2Message());

			if (trackingBranchWasCreated)
			{
				message += std::format("; the new branch '{}' was left behind", resolvedTarget.LocalBranchName);
			}

			return CheckoutOutcome{.Changed = std::move(notified.Updated),
								   .Failure = Error{ErrorCode::IoFailure, std::move(message)}};
		}

		return CheckoutOutcome{.Changed = std::move(notified.Updated), .Failure = std::nullopt};
	}

	Result<void> Repository::DiscardAll(UntrackedFiles untrackedFiles)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		ObjectHandle headCommit;

		if (git_revparse_single(Out(headCommit), m_Handle.get(), "HEAD^{commit}") != 0)
		{
			return Error{ErrorCode::NotFound, std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		TreeHandle headTree;

		if (git_commit_tree(Out(headTree), AsCommit(headCommit.get())) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot read HEAD's tree: {}", LastLibgit2Message())};
		}

		Result<IndexHandle> openedIndex = OpenFreshIndex(m_Handle.get());

		if (!openedIndex.HasValue())
		{
			return openedIndex.GetError();
		}

		const IndexHandle index = std::move(openedIndex).GetValue();

		// Not git_reset, which also deletes rebase-merge/, rebase-apply/, sequencer/ and BISECT_LOG,
		// and discarding or stashing changes in the middle of a rebase must not end it. Untracked files
		// are not removed through REMOVE_UNTRACKED, which would take ignored files in an untracked
		// directory.
		git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
		options.checkout_strategy = GIT_CHECKOUT_FORCE;

		if (git_checkout_tree(m_Handle.get(), AsObject(headTree.get()), &options) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot discard changes: {}", LastLibgit2Message())};
		}

		if (git_index_read_tree(index.get(), headTree.get()) != 0 || git_index_write(index.get()) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot reset the index: {}", LastLibgit2Message())};
		}

		// The untracked files go before git's state files, so that when a state file cannot be removed
		// the tree is already as clean as it was asked to be, and a stash that took the untracked files
		// does not leave them on disk as well.
		if (untrackedFiles == UntrackedFiles::Included)
		{
			Result<void> untrackedRemoval = RemoveUntracked(m_Handle.get(), m_WorkingDirectory);

			if (!untrackedRemoval.HasValue())
			{
				return untrackedRemoval.GetError();
			}
		}

		return EndPendingCommit(m_Handle.get());
	}
}
