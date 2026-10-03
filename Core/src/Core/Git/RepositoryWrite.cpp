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

		// Every write below opens the index the same way and reports the same failure when
		// it cannot, so the three lines live here once.
		Result<IndexHandle> OpenIndex(git_repository* handle)
		{
			IndexHandle index;

			if (git_repository_index(Out(index), handle) != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot read the index: {}", LastLibgit2Message())};
			}

			return index;
		}
	}

	Result<void> Repository::Stage(const std::vector<std::filesystem::path>& paths)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		auto opened = OpenIndex(m_Handle.get());

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		const IndexHandle index = std::move(opened).GetValue();

		for (const std::filesystem::path& path : paths)
		{
			const std::string text = ToGitPath(path);

			// A path that is gone from disk cannot be added; removing its entry is what
			// records the deletion. Asked of the filesystem rather than of the index,
			// because the index is exactly what is being brought up to date.
			std::error_code code;
			const bool onDisk = std::filesystem::exists(m_WorkingDirectory / path, code);

			// exists() answers false on an error too. Taking that for "gone" would record a
			// file that is still there as deleted, so an error stops the stage here. The index
			// is only written at the end, so nothing of it has been touched.
			if (code)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot stage '{}': {}", ToGenericUtf8(path), code.message())};
			}

			int result = 0;

			if (!onDisk)
			{
				result = git_index_remove_bypath(index.get(), text.c_str());
			}
			else if (std::filesystem::is_directory(m_WorkingDirectory / path, code))
			{
				// The status collapses a wholly untracked directory into one entry, and
				// add_bypath takes files only - so the directory is opened up into its files
				// first. QueryUntrackedIn leaves out what .gitignore excludes, the way
				// `git add <dir>` does.
				//
				// Not git_index_add_all: it matches every path it walks against the pathspec
				// as a glob whatever flags it is given, so a directory named with brackets -
				// legal on Windows - would stage nothing and report success.
				const Result<WorkingTreeStatus> files = QueryUntrackedIn(path);

				if (!files.HasValue())
				{
					return files.GetError();
				}

				for (const FileStatus& file : files.GetValue().Entries)
				{
					result = git_index_add_bypath(index.get(), ToGitPath(file.Path).c_str());

					if (result != 0)
					{
						break;
					}
				}
			}
			else
			{
				result = git_index_add_bypath(index.get(), text.c_str());
			}

			if (result != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot stage '{}': {}", text, LastLibgit2Message())};
			}
		}

		const int written = git_index_write(index.get());

		if (written != 0)
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
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		const PathSpec spec{paths};
		const git_strarray pathspec = spec.Get();

		// A null target means "reset to nothing", which removes the entries — the right
		// answer under an unborn HEAD, where there is no commit to restore from.
		ObjectHandle head;
		const bool born = git_revparse_single(Out(head), m_Handle.get(), "HEAD") == 0;

		const int result = git_reset_default(m_Handle.get(), born ? head.get() : nullptr, &pathspec);

		if (result != 0)
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
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		if (paths.empty())
		{
			return Result<void>::Ok();
		}

		const PathSpec spec{paths};

		git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;

		// FORCE is the point of the call: the working tree is being overwritten on purpose,
		// so a checkout that politely refuses because the file differs would do nothing.
		// DISABLE_PATHSPEC_MATCH makes the paths literal rather than glob patterns, so a
		// file whose name contains a bracket is not read as a character class.
		options.checkout_strategy = GIT_CHECKOUT_FORCE | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
		options.paths = spec.Get();

		if (git_checkout_index(m_Handle.get(), nullptr, &options) != 0)
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
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		if (paths.empty())
		{
			return Result<void>::Ok();
		}

		for (const std::filesystem::path& path : paths)
		{
			const std::filesystem::path absolute = m_WorkingDirectory / path;

			std::error_code code;

			// remove() refuses a directory with anything in it, and a status entry for an
			// untracked directory names exactly that. remove_all takes ignored files under
			// it too, which is the only outcome that leaves the directory actually gone.
			if (std::filesystem::is_directory(absolute, code))
			{
				std::filesystem::remove_all(absolute, code);
			}
			else
			{
				std::filesystem::remove(absolute, code);
			}

			if (code)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot delete '{}': {}", ToGitPath(path), code.message())};
			}
		}

		return Result<void>::Ok();
	}

	Result<Oid> Repository::Commit(const CommitRequest& request)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		// git refuses a commit with nothing to say, and so does this: the panel stops it
		// first, but the engine is the backstop. An amend may leave the message empty, which
		// means keeping the one it replaces.
		if (!request.Amend && Trimmed(request.Message).empty())
		{
			return Error{ErrorCode::InvalidArgument, "the commit message is empty"};
		}

		SignatureHandle who;

		// git's own resolution: repository config, then global, then the environment. A
		// repository with no identity fails here, which is the honest answer — the
		// alternative is inventing an author that outlives the mistake in the history.
		if (git_signature_default(Out(who), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot determine who is committing: {}", LastLibgit2Message())};
		}

		auto opened = OpenIndex(m_Handle.get());

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		git_oid treeId{};
		const int written = git_index_write_tree(&treeId, opened.GetValue().get());

		if (written != 0)
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
		const bool born = git_reference_name_to_id(&headId, m_Handle.get(), "HEAD") == 0;

		CommitHandle head;

		if (born && git_commit_lookup(Out(head), m_Handle.get(), &headId) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		Result<Oid> answer = Error{ErrorCode::IoFailure, "unreachable"};
		git_oid commitId{};

		if (request.Amend)
		{
			if (!born)
			{
				return Error{ErrorCode::InvalidArgument,
							 "there is no commit to amend: HEAD is unborn"};
			}

			// No message means "keep the one that is already there", not "replace it with
			// nothing": amending to add a forgotten file is the case with nothing to say,
			// and a wiped message is not something the caller could put back. git_commit_amend
			// reads a null message - and a null encoding, which has to go with it - as leaving
			// that field of the original commit alone.
			const bool keepMessage = request.Message.empty();

			// git_commit_amend keeps the parents of the commit it replaces, which is the
			// whole reason amend is not just "commit with HEAD^ as the parent".
			if (git_commit_amend(&commitId, head.get(), "HEAD", who.get(), who.get(),
								 keepMessage ? nullptr : "UTF-8",
								 keepMessage ? nullptr : request.Message.c_str(), tree.get()) == 0)
			{
				answer = ToOid(commitId);
			}
			else
			{
				answer = Error{ErrorCode::IoFailure,
							   std::format("cannot amend: {}", LastLibgit2Message())};
			}
		}
		else
		{
			const git_commit* parents[1] = {head.get()};

			if (git_commit_create(&commitId, m_Handle.get(), "HEAD", who.get(), who.get(), "UTF-8",
								  request.Message.c_str(), tree.get(), born ? 1u : 0u,
								  born ? parents : nullptr) == 0)
			{
				answer = ToOid(commitId);
			}
			else
			{
				answer = Error{ErrorCode::IoFailure,
							   std::format("cannot commit: {}", LastLibgit2Message())};
			}
		}

		return answer;
	}
}
