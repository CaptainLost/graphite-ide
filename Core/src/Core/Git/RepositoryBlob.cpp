#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstdint>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>

namespace Core {

	Result<std::string> Repository::ReadBlob(const std::filesystem::path& path, const Oid& revision) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_oid raw = ToGitOid(revision);

		git_commit* commit = nullptr;
		if (git_commit_lookup(&commit, m_Handle, &raw) != 0)
		{
			return Error{ErrorCode::NotFound,
						 std::format("cannot look up the commit: {}", LastLibgit2Message())};
		}

		git_tree* tree = nullptr;
		if (git_commit_tree(&tree, commit) != 0)
		{
			const std::string message = LastLibgit2Message();
			git_commit_free(commit);
			return Error{ErrorCode::IoFailure, std::format("cannot read the commit's tree: {}", message)};
		}

		const std::string gitPath = ToGitPath(path);

		git_tree_entry* entry = nullptr;
		const int found = git_tree_entry_bypath(&entry, tree, gitPath.c_str());

		if (found != 0)
		{
			// GIT_ENOTFOUND is the ordinary answer for the older side of a file that was
			// added later — not a failure worth reporting as anything but "absent".
			const ErrorCode code = found == GIT_ENOTFOUND ? ErrorCode::NotFound : ErrorCode::IoFailure;
			const std::string message = LastLibgit2Message();

			git_tree_free(tree);
			git_commit_free(commit);

			return Error{code, std::format("cannot find the path: {}", message)};
		}

		if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB)
		{
			// A directory (or anything else that is not a blob) resolves the path but is
			// not a file the caller can read as one.
			git_tree_entry_free(entry);
			git_tree_free(tree);
			git_commit_free(commit);

			return Error{ErrorCode::NotFound, "the path is not a file"};
		}

		git_oid blobId = *git_tree_entry_id(entry);

		// git_tree_entry_bypath hands back an object the caller owns even though it was
		// read from a tree the caller also owns — one of the few libgit2 lookups with that
		// shape, and easy to leak by assuming otherwise.
		git_tree_entry_free(entry);

		git_blob* blob = nullptr;
		if (git_blob_lookup(&blob, m_Handle, &blobId) != 0)
		{
			const std::string message = LastLibgit2Message();

			git_tree_free(tree);
			git_commit_free(commit);

			return Error{ErrorCode::IoFailure, std::format("cannot read the blob: {}", message)};
		}

		const std::size_t size = static_cast<std::size_t>(git_blob_rawsize(blob));

		// An empty blob is an ordinary thing to commit, and libgit2 may hand back a null
		// pointer for it. std::string's (pointer, count) constructor requires a non-null
		// pointer even when count is zero, so a zero-byte blob needs its own path rather
		// than trusting the pointer unconditionally.
		std::string bytes = size == 0 ? std::string() : std::string(static_cast<const char*>(git_blob_rawcontent(blob)), size);

		git_blob_free(blob);
		git_tree_free(tree);
		git_commit_free(commit);

		return bytes;
	}

	Result<std::optional<Oid>> Repository::FirstParentOf(const Oid& commit) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_oid raw = ToGitOid(commit);

		git_commit* handle = nullptr;
		if (git_commit_lookup(&handle, m_Handle, &raw) != 0)
		{
			return Error{ErrorCode::NotFound,
						 std::format("cannot look up the commit: {}", LastLibgit2Message())};
		}

		if (git_commit_parentcount(handle) == 0)
		{
			// A root commit has nothing before it. Not an error: the caller's answer is
			// the same as for a file that did not exist yet.
			git_commit_free(handle);
			return std::optional<Oid>{};
		}

		git_commit* parent = nullptr;
		const int looked = git_commit_parent(&parent, handle, 0);

		if (looked != 0)
		{
			const std::string message = LastLibgit2Message();
			git_commit_free(handle);
			return Error{ErrorCode::IoFailure, std::format("cannot look up the parent: {}", message)};
		}

		const Oid parentId = ToOid(*git_commit_id(parent));

		git_commit_free(parent);
		git_commit_free(handle);

		return std::optional<Oid>{parentId};
	}

	Result<std::string> Repository::ReadIndexBlob(const std::filesystem::path& path) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_index* index = nullptr;

		if (git_repository_index(&index, m_Handle) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the index: {}", LastLibgit2Message())};
		}

		const std::string gitPath = ToGitPath(path);

		// Stage zero is the settled entry. A path in conflict has stages one to three
		// instead, so this finds nothing for it - which is the answer, not a failure.
		const git_index_entry* entry = git_index_get_bypath(index, gitPath.c_str(), 0);

		if (entry == nullptr)
		{
			git_index_free(index);
			return Error{ErrorCode::NotFound,
						 std::format("'{}' has nothing staged", gitPath)};
		}

		// Copied before the index goes away: the entry points into the index's own storage.
		const git_oid id = entry->id;
		git_index_free(index);

		git_blob* blob = nullptr;

		if (git_blob_lookup(&blob, m_Handle, &id) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the staged blob for '{}': {}", gitPath,
									 LastLibgit2Message())};
		}

		const git_object_size_t size = git_blob_rawsize(blob);

		if (size > MaximumDiffTextSize)
		{
			git_blob_free(blob);
			return Error{ErrorCode::InvalidArgument,
						 std::format("'{}' is too large to show as text ({} bytes)", gitPath, size)};
		}

		std::string bytes{static_cast<const char*>(git_blob_rawcontent(blob)),
						  static_cast<std::size_t>(size)};

		git_blob_free(blob);
		return bytes;
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
}
