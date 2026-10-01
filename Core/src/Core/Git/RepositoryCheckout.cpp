#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <algorithm>
#include <format>
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
			git_reference* reference = nullptr;
			const int result = git_branch_lookup(&reference, handle, name.c_str(), GIT_BRANCH_LOCAL);

			if (result == GIT_ENOTFOUND)
				return false;

			if (result != 0)
			{
				return Error{ErrorCode::InvalidArgument,
							 std::format("cannot look up branch '{}': {}", name, LastLibgit2Message())};
			}

			git_reference_free(reference);
			return true;
		}

		// The commit the target resolves to, and - for a branch - the ref HEAD should name.
		// Owns Commit; the caller frees it on every path.
		struct Resolved
		{
			git_object* Commit = nullptr;
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

			const int result = git_revparse_single(&resolved.Commit, handle, (spec + "^{commit}").c_str());

			if (result != 0)
			{
				return Error{result == GIT_ENOTFOUND ? ErrorCode::NotFound : ErrorCode::InvalidArgument,
							 std::format("cannot resolve '{}': {}", spec, LastLibgit2Message())};
			}

			return resolved;
		}

		// Files the checkout would remove from disk. libgit2 notifies a removal only when it
		// conflicts; a clean one is queued without any notification, so the list is read from
		// the same two trees the checkout diffs - HEAD's and the target's - keeping only what
		// is on disk to be removed. Read before the checkout, which would leave nothing to see.
		Result<std::vector<std::filesystem::path>> Removals(git_repository* handle, git_object* target,
															const std::filesystem::path& workingDirectory)
		{
			std::vector<std::filesystem::path> removed;
			const int unborn = git_repository_head_unborn(handle);

			if (unborn < 0)
				return Error{ErrorCode::IoFailure, std::format("cannot read HEAD: {}", LastLibgit2Message())};

			// No HEAD tree, so nothing tracked to remove.
			if (unborn == 1)
				return removed;

			git_object* headTree = nullptr;

			if (git_revparse_single(&headTree, handle, "HEAD^{tree}") != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot read HEAD's tree: {}", LastLibgit2Message())};

			git_tree* targetTree = nullptr;

			if (git_commit_tree(&targetTree, reinterpret_cast<git_commit*>(target)) != 0)
			{
				git_object_free(headTree);
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read the target's tree: {}", LastLibgit2Message())};
			}

			git_diff* diff = nullptr;
			const int diffed = git_diff_tree_to_tree(&diff, handle, reinterpret_cast<git_tree*>(headTree),
													 targetTree, nullptr);
			git_tree_free(targetTree);
			git_object_free(headTree);

			if (diffed != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot compare the trees: {}", LastLibgit2Message())};

			const std::size_t count = git_diff_num_deltas(diff);

			for (std::size_t i = 0; i < count; ++i)
			{
				const git_diff_delta* delta = git_diff_get_delta(diff, i);

				if (delta->status != GIT_DELTA_DELETED)
					continue;

				std::filesystem::path relative = FromUtf8(delta->old_file.path);
				std::error_code code;

				if (std::filesystem::exists(workingDirectory / relative, code))
					removed.push_back(std::move(relative));
			}

			git_diff_free(diff);
			return removed;
		}

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

			git_status_list* list = nullptr;

			if (git_status_list_new(&list, handle, &options) != 0)
				return Error{ErrorCode::IoFailure, std::format("cannot list untracked files: {}", LastLibgit2Message())};

			std::vector<std::filesystem::path> files;
			const std::size_t count = git_status_list_entrycount(list);

			for (std::size_t i = 0; i < count; ++i)
			{
				const git_status_entry* entry = git_status_byindex(list, i);

				if ((entry->status & GIT_STATUS_WT_NEW) != 0 && entry->index_to_workdir != nullptr)
					files.push_back(FromUtf8(entry->index_to_workdir->old_file.path));
			}

			git_status_list_free(list);

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

		// A removal that conflicts is the user's work, not something the checkout would do.
		void AddRemovals(std::vector<std::filesystem::path>& updated, std::vector<std::filesystem::path> removed,
						 const std::vector<std::filesystem::path>& overwritten)
		{
			for (std::filesystem::path& path : removed)
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

		git_object* commit = resolved.GetValue().Commit;
		auto removed = Removals(m_Handle, commit, m_WorkingDirectory);

		if (!removed.HasValue())
		{
			git_object_free(commit);
			return removed.GetError();
		}

		Notified notified;
		git_checkout_options options = Options(notified, true);

		// A dry run that finds conflicts answers GIT_ECONFLICT, having notified every one of
		// them first; that is the answer, not a failure.
		const int result = git_checkout_tree(m_Handle, commit, &options);
		git_object_free(commit);

		if (result != 0 && result != GIT_ECONFLICT)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot plan the checkout: {}", LastLibgit2Message())};
		}

		AddRemovals(notified.Updated, std::move(removed).GetValue(), notified.Overwritten);
		return CheckoutPlan{std::move(notified.Overwritten), std::move(notified.Updated)};
	}

	Result<std::vector<std::filesystem::path>> Repository::Checkout(const CheckoutTarget& target)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		auto resolved = Resolve(m_Handle, target);

		if (!resolved.HasValue())
			return resolved.GetError();

		const Resolved& r = resolved.GetValue();
		auto removed = Removals(m_Handle, r.Commit, m_WorkingDirectory);

		if (!removed.HasValue())
		{
			git_object_free(r.Commit);
			return removed.GetError();
		}

		// The tracking branch is created before the files move, so that a failure to create it
		// stops the checkout before anything changes; a refused checkout deletes it again.
		git_reference* created = nullptr;

		if (r.CreateTracking)
		{
			if (git_branch_create(&created, m_Handle, r.LocalName.c_str(),
								  reinterpret_cast<git_commit*>(r.Commit), 0) != 0)
			{
				git_object_free(r.Commit);
				return Error{ErrorCode::InvalidArgument,
							 std::format("cannot create branch '{}': {}", r.LocalName, LastLibgit2Message())};
			}

			if (git_branch_set_upstream(created, target.Name.c_str()) != 0)
			{
				std::string message = std::format("cannot make '{}' track '{}': {}", r.LocalName,
												  target.Name, LastLibgit2Message());

				if (git_branch_delete(created) != 0)
					message += std::format("; the new branch could not be removed: {}", LastLibgit2Message());

				git_reference_free(created);
				git_object_free(r.Commit);
				return Error{ErrorCode::IoFailure, std::move(message)};
			}
		}

		Notified notified;
		git_checkout_options options = Options(notified, false);

		// SAFE works out every action before it writes anything, so a conflict is refused
		// with the working tree and the index exactly as they were.
		const int checkedOut = git_checkout_tree(m_Handle, r.Commit, &options);

		if (checkedOut != 0)
		{
			std::string message = checkedOut == GIT_ECONFLICT
									  ? std::format("checking out would overwrite changes: {}", LastLibgit2Message())
									  : std::format("cannot check out: {}", LastLibgit2Message());

			if (created != nullptr)
			{
				if (git_branch_delete(created) != 0)
					message += std::format("; the new branch could not be removed: {}", LastLibgit2Message());

				git_reference_free(created);
			}

			git_object_free(r.Commit);
			return Error{checkedOut == GIT_ECONFLICT ? ErrorCode::InvalidArgument : ErrorCode::IoFailure,
						 std::move(message)};
		}

		const int moved = target.Kind == CheckoutKind::Commit
							  ? git_repository_set_head_detached(m_Handle, git_object_id(r.Commit))
							  : git_repository_set_head(m_Handle, r.HeadRef.c_str());

		git_reference_free(created);
		git_object_free(r.Commit);

		if (moved != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("the files were checked out but HEAD could not be moved: {}",
									 LastLibgit2Message())};
		}

		AddRemovals(notified.Updated, std::move(removed).GetValue(), notified.Overwritten);
		return std::move(notified.Updated);
	}

	Result<void> Repository::DiscardAll(bool includeUntracked)
	{
		if (m_Handle == nullptr)
			return Error{ErrorCode::NotFound, "the repository is not open"};

		git_object* head = nullptr;

		if (git_revparse_single(&head, m_Handle, "HEAD^{commit}") != 0)
			return Error{ErrorCode::NotFound, std::format("cannot read HEAD: {}", LastLibgit2Message())};

		// A hard reset replaces the checkout strategy it is given with FORCE, so untracked
		// files cannot be removed through it - and would not be removed right by it anyway.
		const int result = git_reset(m_Handle, head, GIT_RESET_HARD, nullptr);
		git_object_free(head);

		if (result != 0)
			return Error{ErrorCode::IoFailure, std::format("cannot discard changes: {}", LastLibgit2Message())};

		if (!includeUntracked)
			return Result<void>::Ok();

		return RemoveUntracked(m_Handle, m_WorkingDirectory);
	}
}
