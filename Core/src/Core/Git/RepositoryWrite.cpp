#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <string>
#include <vector>

namespace Core {

	namespace {

		constexpr const char* kMessageEncoding = "UTF-8";

		// A merge may legitimately record HEAD's own tree, because it still joins a second parent.
		bool IsMergeInProgress(git_repository* handle)
		{
			return git_repository_state(handle) == GIT_REPOSITORY_STATE_MERGE;
		}
	}

	Result<void> Repository::Stage(const std::vector<std::filesystem::path>& paths)
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		// Every path is planned before the index is opened: expanding a directory is a status
		// query, which reloads the index and would throw away edits made before the write.
		struct PlannedPath
		{
			std::string GitPath;
			bool IsOnDisk = false;

			bool IsDirectory = false;
			std::vector<std::string> UntrackedFiles;
		};

		std::vector<PlannedPath> plan;
		plan.reserve(paths.size());

		for (const std::filesystem::path& path : paths)
		{
			PlannedPath planned;
			planned.GitPath = ToGitPath(path);

			// Asked of the filesystem, not the index, because the index is what is being brought
			// up to date.
			std::error_code fileSystemError;
			planned.IsOnDisk = std::filesystem::exists(m_WorkingDirectory / path, fileSystemError);

			// exists() also answers false on an error, and taking that for a deletion would record
			// a file that is still there as deleted.
			if (fileSystemError)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot stage '{}': {}", ToGenericUtf8(path), fileSystemError.message())};
			}

			if (planned.IsOnDisk && std::filesystem::is_directory(m_WorkingDirectory / path, fileSystemError))
			{
				// The status collapses a wholly untracked directory into one entry and
				// git_index_add_bypath takes files only, so the directory is expanded through
				// QueryUntrackedIn, which leaves out ignored files as `git add <dir>` does, and not
				// through git_index_add_all: that matches every path against the
				// pathspec as a glob whatever its flags, so a directory named with brackets would
				// stage nothing and report success.
				const Result<WorkingTreeStatus> untrackedInDirectory = QueryUntrackedIn(path);

				if (!untrackedInDirectory.HasValue())
				{
					return untrackedInDirectory.GetError();
				}

				planned.IsDirectory = true;

				for (const FileStatus& untrackedEntry : untrackedInDirectory.GetValue().Entries)
				{
					planned.UntrackedFiles.push_back(ToGitPath(untrackedEntry.Path));
				}
			}

			plan.push_back(std::move(planned));
		}

		Result<IndexHandle> opened = OpenFreshIndex(m_Handle.get());

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		const IndexHandle index = std::move(opened).GetValue();

		for (const PlannedPath& planned : plan)
		{
			int indexEditResult = 0;

			if (!planned.IsOnDisk)
			{
				indexEditResult = git_index_remove_bypath(index.get(), planned.GitPath.c_str());
			}
			else if (planned.IsDirectory)
			{
				for (const std::string& untrackedFile : planned.UntrackedFiles)
				{
					indexEditResult = git_index_add_bypath(index.get(), untrackedFile.c_str());

					if (indexEditResult != 0)
					{
						break;
					}
				}
			}
			else
			{
				indexEditResult = git_index_add_bypath(index.get(), planned.GitPath.c_str());
			}

			if (indexEditResult != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot stage '{}': {}", planned.GitPath, LastLibgit2Message())};
			}
		}

		const int writeResult = git_index_write(index.get());

		if (writeResult != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot write the index: {}", LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}

	Result<void> Repository::Unstage(const std::vector<std::filesystem::path>& paths)
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		// git_reset_default edits the handle's cached index and writes it back.
		if (const Result<void> refreshed = RefreshIndex(m_Handle.get()); !refreshed.HasValue())
		{
			return refreshed.GetError();
		}

		const PathSpec pathSpec{paths};
		const git_strarray pathStrings = pathSpec.Get();

		ObjectHandle head;
		const bool isHeadBorn = git_revparse_single(Out(head), m_Handle.get(), "HEAD") == 0;

		// A null target resets to nothing, which removes the entries: the right answer under an
		// unborn HEAD, where there is no commit to restore from.
		git_object* const resetTarget = isHeadBorn ? head.get() : nullptr;

		const int resetResult = git_reset_default(m_Handle.get(), resetTarget, &pathStrings);

		if (resetResult != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot unstage: {}", LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}

	Result<void> Repository::DiscardWorkTree(const std::vector<std::filesystem::path>& paths)
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		if (paths.empty())
		{
			return Result<void>::Ok();
		}

		// Checking out the index does not reload it, so a stale one would write old content over
		// the files.
		if (const Result<void> refreshed = RefreshIndex(m_Handle.get()); !refreshed.HasValue())
		{
			return refreshed.GetError();
		}

		const PathSpec pathSpec{paths};

		git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;

		// Without FORCE the checkout refuses to overwrite a file that differs, which is the whole
		// point here. DISABLE_PATHSPEC_MATCH keeps a bracket in a file name from reading as a glob.
		options.checkout_strategy = GIT_CHECKOUT_FORCE | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
		options.paths = pathSpec.Get();

		git_index* const useRepositoryIndex = nullptr;

		if (git_checkout_index(m_Handle.get(), useRepositoryIndex, &options) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot discard changes: {}", LastLibgit2Message())};
		}

		return Result<void>::Ok();
	}

	Result<void> Repository::DeleteUntracked(const std::vector<std::filesystem::path>& paths)
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		std::vector<std::filesystem::path> untrackedFiles;

		for (const std::filesystem::path& path : paths)
		{
			std::error_code kindError;
			const bool isDirectory = std::filesystem::is_directory(m_WorkingDirectory / path, kindError);

			if (!isDirectory)
			{
				untrackedFiles.push_back(path);
				continue;
			}

			// An untracked directory's status entry names the directory alone; what goes is what its
			// listing shows, which leaves out the files git ignores.
			Result<WorkingTreeStatus> listing = QueryUntrackedIn(path);

			if (!listing)
			{
				return listing.GetError();
			}

			for (FileStatus& file : listing.GetValue().Entries)
			{
				untrackedFiles.push_back(std::move(file.Path));
			}
		}

		return RemoveUntrackedFiles(m_WorkingDirectory, untrackedFiles);
	}

	Result<Oid> Repository::Commit(const CommitRequest& request)
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		if (!request.Amend && Trimmed(request.Message).empty())
		{
			return Error{ErrorCode::InvalidArgument, "the commit message is empty"};
		}

		SignatureHandle authorAndCommitter;

		// git's own lookup: repository config, then global, then the environment. With no identity
		// this fails rather than inventing an author that would stay in the history.
		if (git_signature_default(Out(authorAndCommitter), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot determine who is committing: {}", LastLibgit2Message())};
		}

		Result<IndexHandle> opened = OpenFreshIndex(m_Handle.get());

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		git_oid treeId{};
		const int writeTreeResult = git_index_write_tree(&treeId, opened.GetValue().get());

		if (writeTreeResult != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot write a tree from the index: {}", LastLibgit2Message())};
		}

		TreeHandle tree;

		if (git_tree_lookup(Out(tree), m_Handle.get(), &treeId) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the tree just written: {}", LastLibgit2Message())};
		}

		git_oid headId{};
		const bool isHeadBorn = git_reference_name_to_id(&headId, m_Handle.get(), "HEAD") == 0;

		CommitHandle head;

		if (isHeadBorn && git_commit_lookup(Out(head), m_Handle.get(), &headId) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		const bool isNewCommitOverHeadTree =
			!request.Amend && isHeadBorn && git_oid_equal(&treeId, git_commit_tree_id(head.get())) != 0;

		if (isNewCommitOverHeadTree && !IsMergeInProgress(m_Handle.get()))
		{
			return Error{ErrorCode::InvalidArgument, "nothing to commit"};
		}

		git_oid commitId{};

		if (request.Amend)
		{
			if (!isHeadBorn)
			{
				return Error{ErrorCode::InvalidArgument,
							 "there is no commit to amend: HEAD is unborn"};
			}

			// git_commit_amend leaves the original's message alone when given a null message, and
			// the encoding has to be null with it.
			const bool keepsMessage = request.Message.empty();
			const char* const messageEncoding = keepsMessage ? nullptr : kMessageEncoding;
			const char* const messageText = keepsMessage ? nullptr : request.Message.c_str();

			// git_commit_amend keeps the parents of the commit it replaces, which a plain commit
			// onto HEAD's parent would not.
			if (git_commit_amend(&commitId, head.get(), "HEAD", authorAndCommitter.get(), authorAndCommitter.get(),
								 messageEncoding, messageText, tree.get()) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot amend: {}", LastLibgit2Message())};
			}

			return ToOid(commitId);
		}

		const git_commit* headAsParent[1] = {head.get()};
		const std::size_t parentCount = isHeadBorn ? 1u : 0u;
		const git_commit** const parents = isHeadBorn ? headAsParent : nullptr;

		if (git_commit_create(&commitId, m_Handle.get(), "HEAD", authorAndCommitter.get(), authorAndCommitter.get(),
							  kMessageEncoding, request.Message.c_str(), tree.get(), parentCount, parents) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot commit: {}", LastLibgit2Message())};
		}

		return ToOid(commitId);
	}
}
