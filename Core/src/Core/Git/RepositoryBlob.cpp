#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace Core {

	namespace {

		constexpr char kUnreadByte = '\0';

		// libgit2 may hand back a null pointer for an empty blob, and std::string's (pointer, count)
		// constructor requires a non-null pointer even when the count is zero.
		std::string BlobBytes(const git_blob* blob)
		{
			const std::size_t byteCount = static_cast<std::size_t>(git_blob_rawsize(blob));

			if (byteCount == 0)
			{
				return std::string();
			}

			return std::string(static_cast<const char*>(git_blob_rawcontent(blob)), byteCount);
		}

		Error TooLargeToShowAsText(const std::string& gitPath, std::uint64_t byteCount)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' is too large to show as text ({} bytes)", gitPath, byteCount)};
		}
	}

	Result<std::string> Repository::ReadBlobById(const Oid& blob) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		const git_oid blobId = ToGitOid(blob);
		BlobHandle found;
		const int lookupResult = git_blob_lookup(Out(found), m_Handle.get(), &blobId);

		if (lookupResult == GIT_ENOTFOUND)
		{
			return Error{ErrorCode::NotFound, std::format("no blob {} in the repository", blob.ShortHex())};
		}

		if (lookupResult != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read blob {}: {}", blob.ShortHex(), LastLibgit2Message())};
		}

		return BlobBytes(found.get());
	}

	Result<std::string> Repository::ReadBlob(const std::filesystem::path& path, const Oid& revision) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<CommitHandle> lookedUp = LookupCommit(m_Handle.get(), revision);

		if (!lookedUp.HasValue())
		{
			return lookedUp.GetError();
		}

		const CommitHandle commit = std::move(lookedUp).GetValue();

		TreeHandle tree;
		if (git_commit_tree(Out(tree), commit.get()) != 0)
		{
			const std::string message = LastLibgit2Message();
			return Error{ErrorCode::IoFailure, std::format("cannot read the commit's tree: {}", message)};
		}

		const std::string gitPath = ToGitPath(path);

		// git_tree_entry_bypath hands back an entry the caller owns even though it was read from a
		// tree the caller also owns, unlike most libgit2 lookups into an owned object.
		TreeEntryHandle entry;
		const int lookupResult = git_tree_entry_bypath(Out(entry), tree.get(), gitPath.c_str());

		if (lookupResult != 0)
		{
			const bool pathAbsentFromCommit = lookupResult == GIT_ENOTFOUND;
			const ErrorCode errorCode = pathAbsentFromCommit ? ErrorCode::NotFound : ErrorCode::IoFailure;
			const std::string message = LastLibgit2Message();

			return Error{errorCode, std::format("cannot find the path: {}", message)};
		}

		const bool pathIsAFile = git_tree_entry_type(entry.get()) == GIT_OBJECT_BLOB;

		if (!pathIsAFile)
		{
			return Error{ErrorCode::NotFound, "the path is not a file"};
		}

		git_oid blobId = *git_tree_entry_id(entry.get());

		BlobHandle blob;
		if (git_blob_lookup(Out(blob), m_Handle.get(), &blobId) != 0)
		{
			const std::string message = LastLibgit2Message();

			return Error{ErrorCode::IoFailure, std::format("cannot read the blob: {}", message)};
		}

		return BlobBytes(blob.get());
	}

	Result<std::optional<Oid>> Repository::FirstParentOf(const Oid& commit) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<CommitHandle> lookedUp = LookupCommit(m_Handle.get(), commit);

		if (!lookedUp.HasValue())
		{
			return lookedUp.GetError();
		}

		const CommitHandle commitHandle = std::move(lookedUp).GetValue();

		const bool isRootCommit = git_commit_parentcount(commitHandle.get()) == 0;

		if (isRootCommit)
		{
			return std::optional<Oid>{};
		}

		CommitHandle parent;
		const int parentLookupResult = git_commit_parent(Out(parent), commitHandle.get(), kFirstParentIndex);

		if (parentLookupResult != 0)
		{
			const std::string message = LastLibgit2Message();
			return Error{ErrorCode::IoFailure, std::format("cannot look up the parent: {}", message)};
		}

		const Oid parentId = ToOid(*git_commit_id(parent.get()));

		return std::optional<Oid>{parentId};
	}

	Result<std::optional<Oid>> Repository::UntrackedOfStash(const Oid& stash) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<CommitHandle> lookedUp = LookupCommit(m_Handle.get(), stash);

		if (!lookedUp.HasValue())
		{
			return lookedUp.GetError();
		}

		const CommitHandle stashCommit = std::move(lookedUp).GetValue();

		if (git_commit_parentcount(stashCommit.get()) <= kUntrackedFilesParentOfStash)
		{
			return std::optional<Oid>{};
		}

		const Oid untrackedFilesCommit = ToOid(*git_commit_parent_id(stashCommit.get(), kUntrackedFilesParentOfStash));

		return std::optional<Oid>{untrackedFilesCommit};
	}

	Result<std::string> Repository::ReadIndexBlob(const std::filesystem::path& path) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		Result<IndexHandle> openedIndex = OpenFreshIndex(m_Handle.get());

		if (!openedIndex.HasValue())
		{
			return openedIndex.GetError();
		}

		const IndexHandle index = std::move(openedIndex).GetValue();
		const std::string gitPath = ToGitPath(path);

		const git_index_entry* settledEntry = git_index_get_bypath(index.get(), gitPath.c_str(), GIT_INDEX_STAGE_NORMAL);

		if (settledEntry == nullptr)
		{
			return Error{ErrorCode::NotFound,
						 std::format("'{}' has nothing staged", gitPath)};
		}

		// Copied rather than pointed at, because the entry points into the index's own storage.
		const git_oid blobId = settledEntry->id;

		BlobHandle blob;

		if (git_blob_lookup(Out(blob), m_Handle.get(), &blobId) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the staged blob for '{}': {}", gitPath,
									 LastLibgit2Message())};
		}

		const git_object_size_t blobSize = git_blob_rawsize(blob.get());

		if (blobSize > MaximumDiffTextSize)
		{
			return TooLargeToShowAsText(gitPath, blobSize);
		}

		return BlobBytes(blob.get());
	}

	Result<std::string> Repository::ReadWorkingTreeFile(const std::filesystem::path& pathRelativeToWorkingDirectory) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		const std::filesystem::path absolutePath = m_WorkingDirectory / pathRelativeToWorkingDirectory;
		const std::string gitPath = ToGitPath(pathRelativeToWorkingDirectory);

		std::error_code fileError;

		if (!std::filesystem::is_regular_file(absolutePath, fileError))
		{
			return Error{ErrorCode::NotFound,
						 std::format("'{}' is not a file in the working tree", gitPath)};
		}

		const std::uintmax_t fileSize = std::filesystem::file_size(absolutePath, fileError);

		if (fileError)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot measure '{}': {}", gitPath, fileError.message())};
		}

		// Refused before anything is allocated, so that an enormous file cannot force the allocation
		// first and fail afterwards.
		if (fileSize > MaximumDiffTextSize)
		{
			return TooLargeToShowAsText(gitPath, fileSize);
		}

		std::ifstream stream{absolutePath, std::ios::binary};

		if (!stream.is_open())
		{
			return Error{ErrorCode::AccessDenied, std::format("cannot open '{}'", gitPath)};
		}

		std::string bytes(static_cast<std::size_t>(fileSize), kUnreadByte);
		stream.read(bytes.data(), static_cast<std::streamsize>(fileSize));

		// The file can shrink between the measurement and the read, and keeping only what was read
		// stops the tail being a run of nul bytes nobody wrote.
		bytes.resize(static_cast<std::size_t>(stream.gcount()));

		return bytes;
	}

	Result<Oid> Repository::HeadCommit() const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		git_oid headId{};

		const int lookupResult = git_reference_name_to_id(&headId, m_Handle.get(), "HEAD");

		if (lookupResult == GIT_ENOTFOUND)
		{
			return Error{ErrorCode::NotFound,
						 std::format("HEAD names no commit: {}", LastLibgit2Message())};
		}

		if (lookupResult != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		return ToOid(headId);
	}
}
