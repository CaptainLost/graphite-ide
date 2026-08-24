#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <string>
#include <vector>

namespace Core {

	namespace {

		// libgit2 wants an array of C strings that outlives the call, so the converted
		// paths are held here and pointed at rather than converted inline.
		struct PathSpec
		{
			explicit PathSpec(const std::vector<std::filesystem::path>& paths)
			{
				Text.reserve(paths.size());
				Pointers.reserve(paths.size());

				for (const std::filesystem::path& path : paths)
				{
					Text.push_back(ToGitPath(path));
				}

				for (const std::string& text : Text)
				{
					Pointers.push_back(text.c_str());
				}

				Array.strings = const_cast<char**>(Pointers.data());
				Array.count = Pointers.size();
			}

			std::vector<std::string> Text;
			std::vector<const char*> Pointers;
			git_strarray Array{};
		};

		// Every write below opens the index the same way and reports the same failure when
		// it cannot, so the three lines live here once.
		Result<git_index*> OpenIndex(git_repository* handle)
		{
			git_index* index = nullptr;

			if (git_repository_index(&index, handle) != 0)
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

		auto opened = OpenIndex(m_Handle);

		if (!opened.HasValue())
		{
			return opened.GetError();
		}

		git_index* index = opened.GetValue();

		for (const std::filesystem::path& path : paths)
		{
			const std::string text = ToGitPath(path);

			// A path that is gone from disk cannot be added; removing its entry is what
			// records the deletion. Asked of the filesystem rather than of the index,
			// because the index is exactly what is being brought up to date.
			std::error_code code;
			const bool onDisk = std::filesystem::exists(m_WorkingDirectory / path, code);

			const int result = onDisk ? git_index_add_bypath(index, text.c_str())
									  : git_index_remove_bypath(index, text.c_str());

			if (result != 0)
			{
				git_index_free(index);
				return Error{ErrorCode::IoFailure,
							 std::format("cannot stage '{}': {}", text, LastLibgit2Message())};
			}
		}

		const int written = git_index_write(index);
		git_index_free(index);

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

		PathSpec spec{paths};

		// A null target means "reset to nothing", which removes the entries — the right
		// answer under an unborn HEAD, where there is no commit to restore from.
		git_object* head = nullptr;
		const bool born = git_revparse_single(&head, m_Handle, "HEAD") == 0;

		const int result = git_reset_default(m_Handle, born ? head : nullptr, &spec.Array);

		if (head != nullptr)
		{
			git_object_free(head);
		}

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

		PathSpec spec{paths};

		git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;

		// FORCE is the point of the call: the working tree is being overwritten on purpose,
		// so a checkout that politely refuses because the file differs would do nothing.
		// DISABLE_PATHSPEC_MATCH makes the paths literal rather than glob patterns, so a
		// file whose name contains a bracket is not read as a character class.
		options.checkout_strategy = GIT_CHECKOUT_FORCE | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
		options.paths = spec.Array;

		if (git_checkout_index(m_Handle, nullptr, &options) != 0)
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
			std::error_code code;
			std::filesystem::remove(m_WorkingDirectory / path, code);

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

		git_signature* who = nullptr;

		// git's own resolution: repository config, then global, then the environment. A
		// repository with no identity fails here, which is the honest answer — the
		// alternative is inventing an author that outlives the mistake in the history.
		if (git_signature_default(&who, m_Handle) != 0)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("cannot determine who is committing: {}", LastLibgit2Message())};
		}

		auto opened = OpenIndex(m_Handle);

		if (!opened.HasValue())
		{
			git_signature_free(who);
			return opened.GetError();
		}

		git_index* index = opened.GetValue();

		git_oid treeId{};
		const int written = git_index_write_tree(&treeId, index);
		git_index_free(index);

		if (written != 0)
		{
			git_signature_free(who);
			return Error{ErrorCode::IoFailure,
						 std::format("cannot write a tree from the index: {}", LastLibgit2Message())};
		}

		git_tree* tree = nullptr;

		if (git_tree_lookup(&tree, m_Handle, &treeId) != 0)
		{
			git_signature_free(who);
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the tree just written: {}", LastLibgit2Message())};
		}

		git_oid headId{};
		const bool born = git_reference_name_to_id(&headId, m_Handle, "HEAD") == 0;

		git_commit* head = nullptr;

		if (born && git_commit_lookup(&head, m_Handle, &headId) != 0)
		{
			git_tree_free(tree);
			git_signature_free(who);
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		Result<Oid> answer = Error{ErrorCode::IoFailure, "unreachable"};
		git_oid commitId{};

		if (request.Amend)
		{
			if (!born)
			{
				git_tree_free(tree);
				git_signature_free(who);
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
			if (git_commit_amend(&commitId, head, "HEAD", who, who,
								 keepMessage ? nullptr : "UTF-8",
								 keepMessage ? nullptr : request.Message.c_str(), tree) == 0)
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
			const git_commit* parents[1] = {head};

			if (git_commit_create(&commitId, m_Handle, "HEAD", who, who, "UTF-8",
								  request.Message.c_str(), tree, born ? 1u : 0u,
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

		if (head != nullptr)
		{
			git_commit_free(head);
		}

		git_tree_free(tree);
		git_signature_free(who);

		return answer;
	}
}
