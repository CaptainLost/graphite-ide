#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace Core {

	namespace {

		// "origin/feature/y" -> "feature/y".
		std::string LocalNameOf(const std::string& remote)
		{
			const auto slash = remote.find('/');
			return slash == std::string::npos ? remote : remote.substr(slash + 1);
		}

		// Only "no such branch" is false: a lookup that failed for another reason is not
		// evidence the branch is absent, and reading it so would create a second one.
		Result<bool> LocalExists(git_repository* handle, const std::string& name)
		{
			ReferenceHandle reference;
			const int result = git_branch_lookup(Out(reference), handle, name.c_str(), GIT_BRANCH_LOCAL);

			if (result == GIT_ENOTFOUND)
				return false;

			if (result != 0)
			{
				return Error{ErrorCode::InvalidArgument,
							 std::format("cannot look up branch '{}': {}", name, LastLibgit2Message())};
			}

			return true;
		}

		// The commit the target resolves to, and - for a branch - the ref HEAD should name.
		struct Resolved
		{
			ObjectHandle Commit;
			std::string HeadRef;
			bool CreateTracking = false;
			std::string LocalName;
		};

		Result<Resolved> Resolve(git_repository* handle, const CheckoutTarget& target)
		{
			Resolved resolved;
			std::string spec;

			switch (target.Kind)
			{
			case CheckoutKind::LocalBranch:
				spec = "refs/heads/" + target.Name;
				resolved.HeadRef = spec;
				break;

			case CheckoutKind::RemoteBranch:
			{
				resolved.LocalName = LocalNameOf(target.Name);

				const auto exists = LocalExists(handle, resolved.LocalName);

				if (!exists.HasValue())
					return exists.GetError();

				if (exists.GetValue())
				{
					spec = "refs/heads/" + resolved.LocalName;
				}
				else
				{
					spec = "refs/remotes/" + target.Name;
					resolved.CreateTracking = true;
				}

				resolved.HeadRef = "refs/heads/" + resolved.LocalName;
				break;
			}

			case CheckoutKind::Commit:
				spec = target.Commit.ToHex();
				break;
			}

			const int result = git_revparse_single(Out(resolved.Commit), handle, (spec + "^{commit}").c_str());

			if (result != 0)
			{
				return Error{result == GIT_ENOTFOUND ? ErrorCode::NotFound : ErrorCode::InvalidArgument,
							 std::format("cannot resolve '{}': {}", spec, LastLibgit2Message())};
			}

			return resolved;
		}

		// HEAD's tree against the target's: what a checkout of a clean working tree would do.
		// Empty with no HEAD yet, which has no tree to compare.
		Result<DiffHandle> HeadToTarget(git_repository* handle, git_object* target)
		{
			DiffHandle diff;
			const int unborn = git_repository_head_unborn(handle);

			if (unborn < 0)
				return Error{ErrorCode::IoFailure, std::format("cannot read HEAD: {}", LastLibgit2Message())};

			if (unborn == 1)
				return diff;

			ObjectHandle headTree;

			if (git_revparse_single(Out(headTree), handle, "HEAD^{tree}") != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot read HEAD's tree: {}", LastLibgit2Message())};

			TreeHandle targetTree;

			if (git_commit_tree(Out(targetTree), reinterpret_cast<git_commit*>(target)) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read the target's tree: {}", LastLibgit2Message())};
			}

			const int diffed = git_diff_tree_to_tree(Out(diff), handle, reinterpret_cast<git_tree*>(headTree.get()),
													 targetTree.get(), nullptr);

			if (diffed != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot compare the trees: {}", LastLibgit2Message())};

			return diff;
		}

		// Files the checkout would remove from disk. libgit2 notifies a removal only when it
		// conflicts; a clean one is queued without any notification, so the list is read from
		// the same two trees the checkout diffs - HEAD's and the target's - keeping only what
		// is on disk to be removed. Read before the checkout, which would leave nothing to see.
		Result<std::vector<std::filesystem::path>> Removals(git_repository* handle, git_object* target,
															const std::filesystem::path& workingDirectory)
		{
			auto diff = HeadToTarget(handle, target);

			if (!diff.HasValue())
				return diff.GetError();

			std::vector<std::filesystem::path> removed;
			const git_diff* deltas = diff.GetValue().get();
			const std::size_t count = deltas != nullptr ? git_diff_num_deltas(deltas) : 0;

			for (std::size_t i = 0; i < count; ++i)
			{
				const git_diff_delta* delta = git_diff_get_delta(deltas, i);

				if (delta->status != GIT_DELTA_DELETED)
					continue;

				std::filesystem::path relative = FromUtf8(delta->old_file.path);
				std::error_code code;

				if (std::filesystem::exists(workingDirectory / relative, code))
					removed.push_back(std::move(relative));
			}

			return removed;
		}

		// Every file HEAD and the target differ in. With unresolved conflicts libgit2 refuses
		// even a dry run before it looks at a single file, so this is the plan's best word on
		// what the checkout will write: the conflicts have to be discarded or stashed before
		// it can go ahead, and that leaves the tree at HEAD.
		Result<std::vector<std::filesystem::path>> Differing(git_repository* handle, git_object* target)
		{
			auto diff = HeadToTarget(handle, target);

			if (!diff.HasValue())
				return diff.GetError();

			std::vector<std::filesystem::path> differing;
			const git_diff* deltas = diff.GetValue().get();
			const std::size_t count = deltas != nullptr ? git_diff_num_deltas(deltas) : 0;

			for (std::size_t i = 0; i < count; ++i)
			{
				const git_diff_delta* delta = git_diff_get_delta(deltas, i);
				const char* path = delta->status == GIT_DELTA_DELETED ? delta->old_file.path : delta->new_file.path;
				differing.push_back(FromUtf8(path));
			}

			return differing;
		}

		// Paths with unresolved conflicts in the index, each once. The index is re-read
		// first: libgit2 keeps one per repository handle, and a merge or a pop that
		// conflicted since it was last read is exactly what this looks for.
		Result<std::vector<std::filesystem::path>> Conflicted(git_repository* handle)
		{
			IndexHandle index;

			// Two statements: Out hands the index over only at the end of the full expression.
			if (git_repository_index(Out(index), handle) != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot read the index: {}", LastLibgit2Message())};

			if (git_index_read(index.get(), false) != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot read the index: {}", LastLibgit2Message())};

			std::vector<std::filesystem::path> conflicted;

			if (!git_index_has_conflicts(index.get()))
				return conflicted;

			// The entries are sorted by path and then stage, so a conflict's two or three
			// stages sit next to each other.
			const std::size_t count = git_index_entrycount(index.get());
			const char* previous = nullptr;

			for (std::size_t i = 0; i < count; ++i)
			{
				const git_index_entry* entry = git_index_get_byindex(index.get(), i);

				if (entry == nullptr || GIT_INDEX_ENTRY_STAGE(entry) == 0)
					continue;

				if (previous == nullptr || std::strcmp(previous, entry->path) != 0)
					conflicted.push_back(FromUtf8(entry->path));

				previous = entry->path;
			}

			return conflicted;
		}

		const char* const ResolveConflictsFirst =
			"the index has unresolved conflicts; resolve or discard the conflicts first";

		struct Notified
		{
			std::vector<std::filesystem::path> Overwritten;
			std::vector<std::filesystem::path> Updated;
		};

		// CONFLICT is what SAFE refuses over: a tracked file changed on disk that the target
		// changes or removes, and an untracked file where the target has one. UPDATED is a
		// file the checkout writes, new or changed; removals never come through here.
		int Notify(git_checkout_notify_t why, const char* path, const git_diff_file*,
				   const git_diff_file*, const git_diff_file*, void* payload)
		{
			auto& notified = *static_cast<Notified*>(payload);

			if (path == nullptr)
				return 0;

			if (why == GIT_CHECKOUT_NOTIFY_CONFLICT)
				notified.Overwritten.push_back(FromUtf8(path));
			else if (why == GIT_CHECKOUT_NOTIFY_UPDATED)
				notified.Updated.push_back(FromUtf8(path));

			return 0;
		}

		git_checkout_options Options(Notified& notified, bool dryRun)
		{
			git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
			options.checkout_strategy = GIT_CHECKOUT_SAFE | (dryRun ? GIT_CHECKOUT_DRY_RUN : 0u);
			options.notify_flags = GIT_CHECKOUT_NOTIFY_CONFLICT | GIT_CHECKOUT_NOTIFY_UPDATED;
			options.notify_cb = Notify;
			options.notify_payload = &notified;
			return options;
		}

		// Untracked files one by one, never a whole directory: libgit2's REMOVE_UNTRACKED
		// deletes an untracked directory with everything in it, ignored files included. The
		// status list leaves ignored files out, so they stay; a directory emptied by this goes
		// too. A directory the list names whole - a nested repository - is left alone, as
		// `git clean -d` leaves one.
		Result<void> RemoveUntracked(git_repository* handle, const std::filesystem::path& workingDirectory)
		{
			git_status_options options = GIT_STATUS_OPTIONS_INIT;
			options.show = GIT_STATUS_SHOW_WORKDIR_ONLY;
			options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

			StatusListHandle list;

			if (git_status_list_new(Out(list), handle, &options) != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot list untracked files: {}", LastLibgit2Message())};

			std::vector<std::filesystem::path> files;
			const std::size_t count = git_status_list_entrycount(list.get());

			for (std::size_t i = 0; i < count; ++i)
			{
				const git_status_entry* entry = git_status_byindex(list.get(), i);

				if ((entry->status & GIT_STATUS_WT_NEW) != 0 && entry->index_to_workdir != nullptr)
					files.push_back(FromUtf8(entry->index_to_workdir->old_file.path));
			}

			for (const std::filesystem::path& relative : files)
			{
				const std::filesystem::path absolute = workingDirectory / relative;
				std::error_code code;

				if (std::filesystem::is_directory(absolute, code))
					continue;

				std::filesystem::remove(absolute, code);

				if (code)
				{
					return Error{ErrorCode::IoFailure,
								 std::format("cannot delete '{}': {}", ToGitPath(relative), code.message())};
				}

				// Up from the file, while each directory is left empty.
				for (std::filesystem::path parent = relative.parent_path(); !parent.empty();
					 parent = parent.parent_path())
				{
					const std::filesystem::path directory = workingDirectory / parent;

					if (!std::filesystem::is_empty(directory, code) || code)
						break;

					std::filesystem::remove(directory, code);

					if (code)
					{
						return Error{ErrorCode::IoFailure,
									 std::format("cannot delete '{}': {}", ToGitPath(parent), code.message())};
					}
				}
			}

			return Result<void>::Ok();
		}

		// What `git reset --hard` removes: the state of a merge, a cherry-pick or a revert that
		// was waiting for its commit, which no longer has anything to commit once the tree and
		// the index are back at HEAD. A rebase, a sequence of picks and a bisect go on across
		// the reset, so their files stay.
		Result<void> EndPendingCommit(git_repository* handle)
		{
			const std::filesystem::path git = FromUtf8(git_repository_path(handle));

			for (const char* name : {"MERGE_HEAD", "MERGE_MSG", "MERGE_MODE", "AUTO_MERGE", "SQUASH_MSG",
									 "CHERRY_PICK_HEAD", "REVERT_HEAD"})
			{
				std::error_code code;
				std::filesystem::remove(git / name, code);

				if (code)
				{
					return Error{ErrorCode::IoFailure,
								 std::format("the changes were discarded but '{}' could not be removed: {}", name,
											 code.message())};
				}
			}

			return Result<void>::Ok();
		}

		// A path that conflicts is the user's work, not something the checkout would do.
		void AddUnlessOverwritten(std::vector<std::filesystem::path>& updated, std::vector<std::filesystem::path> paths,
								  const std::vector<std::filesystem::path>& overwritten)
		{
			for (std::filesystem::path& path : paths)
			{
				if (std::find(overwritten.begin(), overwritten.end(), path) == overwritten.end())
					updated.push_back(std::move(path));
			}
		}
	}

	Result<CheckoutPlan> Repository::PlanCheckout(const CheckoutTarget& target) const
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		auto resolved = Resolve(m_Handle, target);

		if (!resolved.HasValue())
			return resolved.GetError();

		git_object* commit = resolved.GetValue().Commit.get();
		auto conflicted = Conflicted(m_Handle);

		if (!conflicted.HasValue())
			return conflicted.GetError();

		// libgit2 refuses a checkout - a dry run too - over unresolved conflicts without
		// naming a file, which would read as a plan with nothing in the way. They are what
		// is in the way: listed as overwritten, the question opens on them, and Discard or
		// Stash is what clears them.
		if (!conflicted.GetValue().empty())
		{
			auto differing = Differing(m_Handle, commit);

			if (!differing.HasValue())
				return differing.GetError();

			CheckoutPlan plan{std::move(conflicted).GetValue(), {}};
			AddUnlessOverwritten(plan.Updated, std::move(differing).GetValue(), plan.Overwritten);
			return plan;
		}

		auto removed = Removals(m_Handle, commit, m_WorkingDirectory);

		if (!removed.HasValue())
		{
			return removed.GetError();
		}

		Notified notified;
		git_checkout_options options = Options(notified, true);

		// A dry run that finds conflicts answers GIT_ECONFLICT, having notified every one of
		// them first; that is the answer, not a failure.
		const int result = git_checkout_tree(m_Handle, commit, &options);

		if (result != 0 && result != GIT_ECONFLICT)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot plan the checkout: {}", LastLibgit2Message())};
		}

		AddUnlessOverwritten(notified.Updated, std::move(removed).GetValue(), notified.Overwritten);
		return CheckoutPlan{std::move(notified.Overwritten), std::move(notified.Updated)};
	}

	CheckoutOutcome Repository::Checkout(const CheckoutTarget& target)
	{
		const auto failed = [](Error error) { return CheckoutOutcome{{}, std::move(error)}; };

		if (m_Handle == nullptr)
			return failed(Error{ErrorCode::NotFound, "the repository is not open"});

		auto resolved = Resolve(m_Handle, target);

		if (!resolved.HasValue())
			return failed(resolved.GetError());

		const Resolved& r = resolved.GetValue();
		auto conflicted = Conflicted(m_Handle);

		if (!conflicted.HasValue())
			return failed(conflicted.GetError());

		// Refused here, in words that say why: libgit2's own refusal looks like any other
		// "would overwrite changes" one.
		if (!conflicted.GetValue().empty())
			return failed(Error{ErrorCode::InvalidArgument, ResolveConflictsFirst});

		auto removed = Removals(m_Handle, r.Commit.get(), m_WorkingDirectory);

		if (!removed.HasValue())
		{
			return failed(removed.GetError());
		}

		// The tracking branch is created before the files move, so that a failure to create it
		// stops the checkout before anything changes; a failed checkout deletes it again.
		ReferenceHandle created;

		if (r.CreateTracking)
		{
			if (git_branch_create(Out(created), m_Handle, r.LocalName.c_str(),
								  reinterpret_cast<git_commit*>(r.Commit.get()), 0) != 0)
			{
				return failed(Error{ErrorCode::InvalidArgument,
									std::format("cannot create branch '{}': {}", r.LocalName, LastLibgit2Message())});
			}

			if (git_branch_set_upstream(created.get(), target.Name.c_str()) != 0)
			{
				std::string message = std::format("cannot make '{}' track '{}': {}", r.LocalName,
												  target.Name, LastLibgit2Message());

				if (git_branch_delete(created.get()) != 0)
					message += std::format("; the new branch could not be removed: {}", LastLibgit2Message());

				return failed(Error{ErrorCode::IoFailure, std::move(message)});
			}
		}

		Notified notified;
		git_checkout_options options = Options(notified, false);

		// SAFE works out - and notifies - every action before it writes anything, so a conflict
		// is refused with the working tree and the index exactly as they were, and by the time
		// any other failure can happen, Updated already holds everything the checkout meant to
		// write.
		const int checkedOut = git_checkout_tree(m_Handle, r.Commit.get(), &options);
		AddUnlessOverwritten(notified.Updated, std::move(removed).GetValue(), notified.Overwritten);

		if (checkedOut != 0)
		{
			const bool refused = checkedOut == GIT_ECONFLICT;
			std::string message = refused
									  ? std::format("checking out would overwrite changes: {}", LastLibgit2Message())
									  : std::format("the checkout failed partway and the working tree may be partly "
													"switched; HEAD has not moved: {}",
													LastLibgit2Message());

			if (created != nullptr)
			{
				if (git_branch_delete(created.get()) != 0)
					message += std::format("; the new branch could not be removed: {}", LastLibgit2Message());
			}

			// A refusal changed nothing. Any other failure may have written or removed any of
			// the planned paths, so all of them are named.
			if (refused)
				return failed(Error{ErrorCode::InvalidArgument, std::move(message)});

			return CheckoutOutcome{std::move(notified.Updated), Error{ErrorCode::IoFailure, std::move(message)}};
		}

		const int moved = target.Kind == CheckoutKind::Commit
							  ? git_repository_set_head_detached(m_Handle, git_object_id(r.Commit.get()))
							  : git_repository_set_head(m_Handle, r.HeadRef.c_str());

		const bool branchCreated = created != nullptr;

		if (moved != 0)
		{
			std::string message = std::format("the files were checked out but HEAD could not be moved: {}",
											  LastLibgit2Message());

			if (branchCreated)
				message += std::format("; the new branch '{}' was left behind", r.LocalName);

			return CheckoutOutcome{std::move(notified.Updated), Error{ErrorCode::IoFailure, std::move(message)}};
		}

		return CheckoutOutcome{std::move(notified.Updated), std::nullopt};
	}

	Result<void> Repository::DiscardAll(bool includeUntracked)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		ObjectHandle head;

		if (git_revparse_single(Out(head), m_Handle, "HEAD^{commit}") != 0)
			return Error{ErrorCode::NotFound, std::format("cannot read HEAD: {}", LastLibgit2Message())};

		TreeHandle tree;

		if (git_commit_tree(Out(tree), reinterpret_cast<git_commit*>(head.get())) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot read HEAD's tree: {}", LastLibgit2Message())};

		IndexHandle index;

		if (git_repository_index(Out(index), m_Handle) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot read the index: {}", LastLibgit2Message())};

		// What a hard reset does, but with git's own ending: git_reset also deletes
		// rebase-merge/, rebase-apply/, sequencer/ and BISECT_LOG, and discarding the changes -
		// or stashing them - in the middle of a rebase must not end it. FORCE writes the tree
		// over every change, conflicts included, and reading the tree into the index drops
		// whatever was staged. Untracked files are not removed through REMOVE_UNTRACKED, which
		// would take ignored files in an untracked directory.
		git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
		options.checkout_strategy = GIT_CHECKOUT_FORCE;

		if (git_checkout_tree(m_Handle, reinterpret_cast<git_object*>(tree.get()), &options) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot discard changes: {}", LastLibgit2Message())};

		if (git_index_read_tree(index.get(), tree.get()) != 0 || git_index_write(index.get()) != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot reset the index: {}", LastLibgit2Message())};

		auto ended = EndPendingCommit(m_Handle);

		if (!ended.HasValue())
			return ended.GetError();

		if (!includeUntracked)
			return Result<void>::Ok();

		return RemoveUntracked(m_Handle, m_WorkingDirectory);
	}
}
