#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstdint>
#include <string>
#include <utility>

namespace Core {

	namespace {
		// Compared as components rather than as a string, so an in-tree file called "..config"
		// is not mistaken for a leading "..".
		bool LeadsOutOfTree(const std::filesystem::path& relative)
		{
			return relative.begin() != relative.end() && *relative.begin() == "..";
		}
	}

	Result<Repository> Repository::Open(const std::filesystem::path& anyPathInside)
	{
		EnsureLibgit2Initialised();

		git_repository* handle = nullptr;
		const std::string path = ToUtf8(anyPathInside);

		// ceiling_dirs is null so the search walks up to the filesystem root, matching
		// what the git command line does from a subdirectory.
		const int result = git_repository_open_ext(&handle, path.c_str(), 0, nullptr);

		if (result != 0 || handle == nullptr)
		{
			const std::string message = LastLibgit2Message();

			ErrorCode code = ErrorCode::IoFailure;

			if (result == GIT_ENOTFOUND)
				code = ErrorCode::NotFound;
			else if (result == GIT_EAUTH)
				code = ErrorCode::AccessDenied;

			return Error{code, std::format("cannot open a git repository at '{}': {}", path, message)};
		}

		Repository repository;
		repository.m_Handle.reset(handle);

		if (const char* workdir = git_repository_workdir(handle); workdir != nullptr)
			repository.m_WorkingDirectory = FromUtf8(workdir);

		return repository;
	}

	Result<Repository> Repository::Init(const std::filesystem::path& directory)
	{
		EnsureLibgit2Initialised();

		git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;

		// Without MKPATH, the default: the folder is the one open in the IDE, so it exists,
		// and creating a missing one would hide a wrong path behind a fresh repository.
		options.flags = 0;

		git_repository* handle = nullptr;
		const std::string path = ToUtf8(directory);

		if (git_repository_init_ext(&handle, path.c_str(), &options) != 0 || handle == nullptr)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot initialise a git repository at '{}': {}", path,
									 LastLibgit2Message())};
		}

		Repository repository;
		repository.m_Handle.reset(handle);

		if (const char* workdir = git_repository_workdir(handle); workdir != nullptr)
			repository.m_WorkingDirectory = FromUtf8(workdir);

		return repository;
	}

	void Repository::Free::operator()(git_repository* repository) const noexcept
	{
		git_repository_free(repository);
	}

	bool Repository::IsIgnored(const std::filesystem::path& path) const
	{
		if (m_Handle == nullptr)
			return false;

		std::error_code code;
		const std::filesystem::path relative =
			std::filesystem::relative(path, m_WorkingDirectory, code);

		if (code || relative.empty() || LeadsOutOfTree(relative))
			return false;

		const std::string text = ToGitPath(relative);

		int ignored = 0;
		if (git_ignore_path_is_ignored(&ignored, m_Handle.get(), text.c_str()) != 0)
		{
			CORE_TRACE("Cannot determine ignore status for '{}'", text);
			return false;
		}

		return ignored != 0;
	}

	bool Repository::IsInsideSubmodule(const std::filesystem::path& relative) const
	{
		if (m_Handle == nullptr || relative.empty() || LeadsOutOfTree(relative))
			return false;

		IndexHandle index;

		if (git_repository_index(Out(index), m_Handle.get()) != 0)
		{
			CORE_TRACE("Cannot read the index to look for a submodule above '{}'", ToGitPath(relative));
			return false;
		}

		bool inside = false;
		std::filesystem::path prefix;

		// Only the directories above the path: a gitlink at the path itself is the submodule's
		// root directory, not a file inside it.
		for (const std::filesystem::path& part : relative.parent_path())
		{
			prefix /= part;

			const std::string text = ToGitPath(prefix);
			const git_index_entry* entry = git_index_get_bypath(index.get(), text.c_str(), 0);

			if (entry != nullptr && entry->mode == static_cast<std::uint32_t>(GIT_FILEMODE_COMMIT))
			{
				inside = true;
				break;
			}
		}

		return inside;
	}
}