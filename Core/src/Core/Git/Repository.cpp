#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <utility>

namespace Core {

	namespace {
		void EnsureInitialised()
		{
			static const int result = git_libgit2_init();

			if (result < 0)
				CORE_ERROR("libgit2 failed to initialise (code {})", result);
		}

		// Compared as components rather than as a string, so an in-tree file called "..config"
		// is not mistaken for a leading "..".
		bool LeadsOutOfTree(const std::filesystem::path& relative)
		{
			return relative.begin() != relative.end() && *relative.begin() == "..";
		}
	}

	Result<Repository> Repository::Open(const std::filesystem::path& anyPathInside)
	{
		EnsureInitialised();

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
		repository.m_Handle = handle;

		if (const char* workdir = git_repository_workdir(handle); workdir != nullptr)
			repository.m_WorkingDirectory = FromUtf8(workdir);

		return repository;
	}

	Repository::~Repository()
	{
		if (m_Handle != nullptr)
			git_repository_free(m_Handle);
	}

	Repository::Repository(Repository&& other) noexcept
		: m_Handle(std::exchange(other.m_Handle, nullptr)),
		  m_WorkingDirectory(std::move(other.m_WorkingDirectory))
	{
	}

	Repository& Repository::operator=(Repository&& other) noexcept
	{
		if (this == &other)
			return *this;

		if (m_Handle != nullptr)
			git_repository_free(m_Handle);

		m_Handle = std::exchange(other.m_Handle, nullptr);
		m_WorkingDirectory = std::move(other.m_WorkingDirectory);
		return *this;
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
		if (git_ignore_path_is_ignored(&ignored, m_Handle, text.c_str()) != 0)
		{
			CORE_TRACE("Cannot determine ignore status for '{}'", text);
			return false;
		}

		return ignored != 0;
	}
}