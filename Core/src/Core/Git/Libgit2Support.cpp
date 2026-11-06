#include "corepch.h"

#include "Core/Git/Libgit2Support.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"

#include <git2.h>
#include <git2/sys/errors.h>

#include <chrono>
#include <cstddef>
#include <format>
#include <string_view>
#include <system_error>

namespace Core {

	namespace {

		constexpr std::string_view kCarriageReturnLineFeed = "\r\n";
		constexpr std::string_view kLineFeed = "\n";
		constexpr std::size_t kNotALineTerminator = std::string_view::npos;

		// Git does not normalise line endings inside a commit message, so a message written with
		// CRLF keeps it.
		std::size_t OffsetAfterLineTerminator(std::string_view text, std::size_t terminatorStart)
		{
			if (terminatorStart >= text.size())
			{
				return kNotALineTerminator;
			}

			const std::string_view rest = text.substr(terminatorStart);

			if (rest.starts_with(kCarriageReturnLineFeed))
			{
				return terminatorStart + kCarriageReturnLineFeed.size();
			}

			if (rest.starts_with(kLineFeed))
			{
				return terminatorStart + kLineFeed.size();
			}

			return kNotALineTerminator;
		}

		Error IndexReadFailure()
		{
			return Error{.Code = ErrorCode::IoFailure,
						 .Message = std::format("cannot read the index: {}", LastLibgit2Message())};
		}
	}

	std::string LastLibgit2Message()
	{
		const git_error* lastError = git_error_last();
		const bool hasMessage = lastError != nullptr && lastError->message != nullptr;

		return hasMessage ? lastError->message : "unknown error";
	}

	Result<IndexHandle> OpenFreshIndex(git_repository* handle)
	{
		IndexHandle index;

		if (git_repository_index(Out(index), handle) != 0)
		{
			return IndexReadFailure();
		}

		const bool reloadEvenIfChecksumUnchanged = false;

		if (git_index_read(index.get(), reloadEvenIfChecksumUnchanged) != 0)
		{
			return IndexReadFailure();
		}

		return index;
	}

	Result<void> RefreshIndex(git_repository* handle)
	{
		Result<IndexHandle> openResult = OpenFreshIndex(handle);

		if (!openResult.HasValue())
		{
			return openResult.GetError();
		}

		return Result<void>::Ok();
	}

	std::string TextOrEmpty(const char* text)
	{
		return text != nullptr ? text : "";
	}

	const char* DataOrEmptyString(std::string_view text)
	{
		return text.empty() ? "" : text.data();
	}

	Error RepositoryNotOpen()
	{
		return Error{ErrorCode::NotFound, "the repository is not open"};
	}

	Result<CommitHandle> LookupCommit(git_repository* handle, const Oid& id)
	{
		const git_oid commitId = ToGitOid(id);
		CommitHandle commit;

		if (git_commit_lookup(Out(commit), handle, &commitId) != 0)
		{
			return Error{ErrorCode::NotFound, std::format("cannot find commit {}: {}", id.ToHex(), LastLibgit2Message())};
		}

		return commit;
	}

	bool IsMissingObject(int result)
	{
		if (result == GIT_ENOTFOUND)
		{
			return true;
		}

		const git_error* const lastError = git_error_last();
		return lastError != nullptr && lastError->klass == GIT_ERROR_ODB;
	}

	Error ObjectReadFailure(int result, std::string_view action)
	{
		const ErrorCode code = IsMissingObject(result) ? ErrorCode::NotFound : ErrorCode::IoFailure;
		return Error{code, std::format("{}: {}", action, LastLibgit2Message())};
	}

	Result<DiffHandle> DiffWithFirstParent(git_repository* handle, git_commit* commit)
	{
		TreeHandle tree;
		git_error_clear();

		if (const int treeResult = git_commit_tree(Out(tree), commit); treeResult != 0)
		{
			return ObjectReadFailure(treeResult, "cannot read a commit's tree");
		}

		// A root commit is diffed against the empty tree, which libgit2 takes as a null one.
		TreeHandle parentTree;

		if (git_commit_parentcount(commit) > 0)
		{
			CommitHandle parent;
			git_error_clear();

			if (const int parentResult = git_commit_parent(Out(parent), commit, kFirstParentIndex); parentResult != 0)
			{
				return ObjectReadFailure(parentResult, "cannot read a commit's first parent");
			}

			git_error_clear();

			if (const int parentTreeResult = git_commit_tree(Out(parentTree), parent.get()); parentTreeResult != 0)
			{
				return ObjectReadFailure(parentTreeResult, "cannot read a commit's first parent's tree");
			}
		}

		DiffHandle diff;
		git_error_clear();

		if (const int diffResult = git_diff_tree_to_tree(Out(diff), handle, parentTree.get(), tree.get(), nullptr);
			diffResult != 0)
		{
			return ObjectReadFailure(diffResult, "cannot compare a commit with its first parent");
		}

		return diff;
	}

	std::optional<std::string> HeadReferenceName(git_repository* handle)
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

	Signature ToSignature(const git_signature* signature)
	{
		if (signature == nullptr)
		{
			return Signature{};
		}

		return Signature{.Name = TextOrEmpty(signature->name),
						 .Email = TextOrEmpty(signature->email),
						 .When = std::chrono::sys_seconds{std::chrono::seconds{signature->when.time}}};
	}

	CommitSummary ToCommitSummary(git_commit* commit)
	{
		CommitSummary summary;
		summary.Id = ToOid(*git_commit_id(commit));

		const unsigned int parentCount = git_commit_parentcount(commit);
		summary.Parents.reserve(parentCount);

		for (unsigned int parentIndex = 0; parentIndex < parentCount; ++parentIndex)
		{
			summary.Parents.push_back(ToOid(*git_commit_parent_id(commit, parentIndex)));
		}

		summary.Author = ToSignature(git_commit_author(commit));
		summary.Committer = ToSignature(git_commit_committer(commit));

		if (const char* summaryLine = git_commit_summary(commit); summaryLine != nullptr)
		{
			summary.Summary = summaryLine;
		}

		// git_commit_body() trims the surrounding whitespace and loses the trailing newline the
		// message was written with, so the body is cut out of the raw message instead.
		if (const char* rawMessage = git_commit_message(commit); rawMessage != nullptr)
		{
			const std::string_view message(rawMessage);
			const std::size_t titleEnd = message.find_first_of("\r\n");

			if (titleEnd != std::string_view::npos)
			{
				std::size_t bodyStart = OffsetAfterLineTerminator(message, titleEnd);

				if (bodyStart != kNotALineTerminator)
				{
					// Only the one blank line that separates title from body is skipped. A second
					// blank line is the body's own, as `git log --format=%b` shows it.
					if (const std::size_t afterSeparatorLine = OffsetAfterLineTerminator(message, bodyStart);
						afterSeparatorLine != kNotALineTerminator)
					{
						bodyStart = afterSeparatorLine;
					}

					if (bodyStart < message.size())
					{
						summary.Body = message.substr(bodyStart);
					}
				}
			}
		}

		return summary;
	}

	Oid ToOid(const git_oid& id)
	{
		Oid oid;

		for (std::size_t byteIndex = 0; byteIndex < oid.Bytes.size(); ++byteIndex)
		{
			oid.Bytes[byteIndex] = static_cast<std::uint8_t>(id.id[byteIndex]);
		}

		return oid;
	}

	git_oid ToGitOid(const Oid& id)
	{
		git_oid gitOid{};

		for (std::size_t byteIndex = 0; byteIndex < id.Bytes.size(); ++byteIndex)
		{
			gitOid.id[byteIndex] = id.Bytes[byteIndex];
		}

		return gitOid;
	}

	std::string ToGitPath(const std::filesystem::path& relative)
	{
		std::string gitPath = ToUtf8(relative);

		for (char& character : gitPath)
		{
			if (character == '\\')
			{
				character = '/';
			}
		}

		return gitPath;
	}

	PathSpec::PathSpec(const std::vector<std::filesystem::path>& paths)
	{
		m_GitPaths.reserve(paths.size());
		m_GitPathPointers.reserve(paths.size());

		for (const std::filesystem::path& path : paths)
		{
			m_GitPaths.push_back(ToGitPath(path));
		}

		for (const std::string& gitPath : m_GitPaths)
		{
			m_GitPathPointers.push_back(gitPath.c_str());
		}
	}

	git_strarray PathSpec::Get() const
	{
		// The one cast libgit2's signature forces: git_strarray holds char**, though every call
		// that takes a pathspec only reads it.
		return git_strarray{.strings = const_cast<char**>(m_GitPathPointers.data()), .count = m_GitPathPointers.size()};
	}

	Result<void> RemoveUntrackedFiles(const std::filesystem::path& workingDirectory,
									  const std::vector<std::filesystem::path>& relativeFiles)
	{
		for (const std::filesystem::path& relativePath : relativeFiles)
		{
			const std::filesystem::path absolutePath = workingDirectory / relativePath;
			std::error_code fileSystemError;

			if (std::filesystem::is_directory(absolutePath, fileSystemError))
			{
				continue;
			}

			std::filesystem::remove(absolutePath, fileSystemError);

			if (fileSystemError)
			{
				return Error{ErrorCode::IoFailure,
							 std::format("cannot delete '{}': {}", ToGitPath(relativePath), fileSystemError.message())};
			}

			for (std::filesystem::path parentDirectory = relativePath.parent_path(); !parentDirectory.empty();
				 parentDirectory = parentDirectory.parent_path())
			{
				const std::filesystem::path absoluteDirectory = workingDirectory / parentDirectory;
				const bool directoryIsNowEmpty = std::filesystem::is_empty(absoluteDirectory, fileSystemError);

				if (!directoryIsNowEmpty || fileSystemError)
				{
					break;
				}

				std::filesystem::remove(absoluteDirectory, fileSystemError);

				if (fileSystemError)
				{
					return Error{ErrorCode::IoFailure, std::format("cannot delete '{}': {}", ToGitPath(parentDirectory),
																   fileSystemError.message())};
				}
			}
		}

		return Result<void>::Ok();
	}

	void EnsureLibgit2Initialised()
	{
		static const int initialiseCode = git_libgit2_init();

		if (initialiseCode < 0)
		{
			CORE_ERROR("libgit2 failed to initialise (code {})", initialiseCode);
		}
	}
}
