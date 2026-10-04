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

	Result<void> Repository::Stage(const std::vector<std::filesystem::path>& paths)
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		// What each path stands for, all worked out before the index is opened: opening a
		// directory up into its files is a status query, and that reloads the index - which,
		// between this operation's own edits and its write, would throw those edits away.
		struct Planned
		{
			std::string Text;
			bool OnDisk = false;

			bool Directory = false;

			// For a directory, the untracked files it is opened up into.
			std::vector<std::string> Files;
		};

		std::vector<Planned> plan;
		plan.reserve(paths.size());

		for (const std::filesystem::path& path : paths)
		{
			Planned planned;
			planned.Text = ToGitPath(path);

			// A path that is gone from disk cannot be added; removing its entry is what
			// records the deletion. Asked of the filesystem rather than of the index,
			// because the index is exactly what is being brought up to date.
			std::error_code code;
			planned.OnDisk = std::filesystem::exists(m_WorkingDirectory / path, code);

			// exists() answers false on an error too. Taking that for "gone" would record a
			// file that is still there as deleted, so an error stops the stage here. The index
			// is only written at the end, so nothing of it has been touched.
			if (code)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot stage '{}': {}", ToGenericUtf8(path), code.message())};
			}

			if (planned.OnDisk && std::filesystem::is_directory(m_WorkingDirectory / path, code))
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

				planned.Directory = true;

				for (const FileStatus& file : files.GetValue().Entries)
				{
					planned.Files.push_back(ToGitPath(file.Path));
				}
			}

			plan.push_back(std::move(planned));
		}

		auto opened = OpenFreshIndex(m_Handle.get());

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		const IndexHandle index = std::move(opened).GetValue();

		for (const Planned& planned : plan)
		{
			int result = 0;

			if (!planned.OnDisk)
			{
				result = git_index_remove_bypath(index.get(), planned.Text.c_str());
			}
			else if (planned.Directory)
			{
				for (const std::string& file : planned.Files)
				{
					result = git_index_add_bypath(index.get(), file.c_str());

					if (result != 0)
					{
						break;
					}
				}
			}
			else
			{
				result = git_index_add_bypath(index.get(), planned.Text.c_str());
			}

			if (result != 0)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot stage '{}': {}", planned.Text, LastLibgit2Message())};
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

		// git_reset_default edits the handle's cached index and writes it back.
		if (const auto refreshed = RefreshIndex(m_Handle.get()); !refreshed.HasValue())
		{
			return refreshed.GetError();
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

		// Checking out the index does not reload it, so a stale one would write old content over
		// the files.
		if (const auto refreshed = RefreshIndex(m_Handle.get()); !refreshed.HasValue())
		{
			return refreshed.GetError();
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

		auto opened = OpenFreshIndex(m_Handle.get());

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

		// A commit whose tree is its parent's records nothing, and git refuses it unless asked
		// to allow it. The panel stops it first; this is the backstop for a caller that did not.
		// An amend is exempt - rewording a commit changes it with the tree untouched - and so
		// is a merge, whose result may legitimately be HEAD's own tree: it still joins a
		// second parent.
		if (!request.Amend && born && git_oid_equal(&treeId, git_commit_tree_id(head.get())) != 0 &&
			git_repository_state(m_Handle.get()) != GIT_REPOSITORY_STATE_MERGE)
		{
			return Error{ErrorCode::InvalidArgument, "nothing to commit"};
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
