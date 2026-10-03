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

		// An empty blob is an ordinary thing to commit or stage, and libgit2 may hand back
		// a null pointer for it. std::string's (pointer, count) constructor requires a
		// non-null pointer even when count is zero, so a zero-byte blob needs its own path
		// rather than trusting the pointer unconditionally.
		std::string BlobBytes(const git_blob* blob)
		{
			const std::size_t size = static_cast<std::size_t>(git_blob_rawsize(blob));

			if (size == 0)
			{
				return std::string();
			}

			return std::string(static_cast<const char*>(git_blob_rawcontent(blob)), size);
		}
	}

	Result<std::string> Repository::ReadBlob(const std::filesystem::path& path, const Oid& revision) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_oid raw = ToGitOid(revision);

		CommitHandle commit;
		if (git_commit_lookup(Out(commit), m_Handle.get(), &raw) != 0)
		{
			return Error{ErrorCode::NotFound,
						 std::format("cannot look up the commit: {}", LastLibgit2Message())};
		}

		TreeHandle tree;
		if (git_commit_tree(Out(tree), commit.get()) != 0)
		{
			const std::string message = LastLibgit2Message();
			return Error{ErrorCode::IoFailure, std::format("cannot read the commit's tree: {}", message)};
		}

		const std::string gitPath = ToGitPath(path);

		// git_tree_entry_bypath hands back an object the caller owns even though it was
		// read from a tree the caller also owns — one of the few libgit2 lookups with that
		// shape, and easy to leak by assuming otherwise.
		TreeEntryHandle entry;
		const int found = git_tree_entry_bypath(Out(entry), tree.get(), gitPath.c_str());

		if (found != 0)
		{
			// GIT_ENOTFOUND is the ordinary answer for the older side of a file that was
			// added later — not a failure worth reporting as anything but "absent".
			const ErrorCode code = found == GIT_ENOTFOUND ? ErrorCode::NotFound : ErrorCode::IoFailure;
			const std::string message = LastLibgit2Message();

			return Error{code, std::format("cannot find the path: {}", message)};
		}

		if (git_tree_entry_type(entry.get()) != GIT_OBJECT_BLOB)
		{
			// A directory (or anything else that is not a blob) resolves the path but is
			// not a file the caller can read as one.
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
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_oid raw = ToGitOid(commit);

		CommitHandle handle;
		if (git_commit_lookup(Out(handle), m_Handle.get(), &raw) != 0)
		{
			return Error{ErrorCode::NotFound,
						 std::format("cannot look up the commit: {}", LastLibgit2Message())};
		}

		if (git_commit_parentcount(handle.get()) == 0)
		{
			// A root commit has nothing before it. Not an error: the caller's answer is
			// the same as for a file that did not exist yet.
			return std::optional<Oid>{};
		}

		CommitHandle parent;
		const int looked = git_commit_parent(Out(parent), handle.get(), 0);

		if (looked != 0)
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
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_oid raw = ToGitOid(stash);

		CommitHandle handle;
		if (git_commit_lookup(Out(handle), m_Handle.get(), &raw) != 0)
		{
			return Error{ErrorCode::NotFound,
						 std::format("cannot look up the commit: {}", LastLibgit2Message())};
		}

		if (git_commit_parentcount(handle.get()) < 3)
		{
			return std::optional<Oid>{};
		}

		return std::optional<Oid>{ToOid(*git_commit_parent_id(handle.get(), 2))};
	}

	Result<std::string> Repository::ReadIndexBlob(const std::filesystem::path& path) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		IndexHandle index;

		if (git_repository_index(Out(index), m_Handle.get()) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the index: {}", LastLibgit2Message())};
		}

		const std::string gitPath = ToGitPath(path);

		// Stage zero is the settled entry. A path in conflict has stages one to three
		// instead, so this finds nothing for it - which is the answer, not a failure.
		const git_index_entry* entry = git_index_get_bypath(index.get(), gitPath.c_str(), 0);

		if (entry == nullptr)
		{
			return Error{ErrorCode::NotFound,
						 std::format("'{}' has nothing staged", gitPath)};
		}

		// Copied rather than pointed at: the entry points into the index's own storage.
		const git_oid id = entry->id;

		BlobHandle blob;

		if (git_blob_lookup(Out(blob), m_Handle.get(), &id) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the staged blob for '{}': {}", gitPath,
									 LastLibgit2Message())};
		}

		const git_object_size_t size = git_blob_rawsize(blob.get());

		if (size > MaximumDiffTextSize)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' is too large to show as text ({} bytes)", gitPath, size)};
		}

		return BlobBytes(blob.get());
	}

	Result<std::string> Repository::ReadWorkingTreeFile(const std::filesystem::path& path) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		const std::filesystem::path full = m_WorkingDirectory / path;
		const std::string gitPath = ToGitPath(path);

		std::error_code code;

		if (!std::filesystem::is_regular_file(full, code))
		{
			return Error{ErrorCode::NotFound,
						 std::format("'{}' is not a file in the working tree", gitPath)};
		}

		const std::uintmax_t size = std::filesystem::file_size(full, code);

		if (code)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot measure '{}': {}", gitPath, code.message())};
		}

		// Refused before anything is allocated, so an enormous file cannot force the
		// allocation first and fail afterwards.
		if (size > MaximumDiffTextSize)
		{
			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' is too large to show as text ({} bytes)", gitPath, size)};
		}

		std::ifstream stream{full, std::ios::binary};

		if (!stream.is_open())
		{
			return Error{ErrorCode::AccessDenied, std::format("cannot open '{}'", gitPath)};
		}

		std::string bytes(static_cast<std::size_t>(size), '\0');
		stream.read(bytes.data(), static_cast<std::streamsize>(size));

		// The file can shrink between the measurement and the read; keeping only what was
		// actually read is what stops the tail being a run of nul bytes nobody wrote.
		bytes.resize(static_cast<std::size_t>(stream.gcount()));

		return bytes;
	}

	Result<Oid> Repository::HeadCommit() const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_oid id{};

		const int found = git_reference_name_to_id(&id, m_Handle.get(), "HEAD");

		if (found == GIT_ENOTFOUND)
		{
			return Error{ErrorCode::NotFound,
						 std::format("HEAD names no commit: {}", LastLibgit2Message())};
		}

		if (found != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read HEAD: {}", LastLibgit2Message())};
		}

		return ToOid(id);
	}
}
