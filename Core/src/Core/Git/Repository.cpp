#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

	namespace {

		constexpr const char* kParentDirectoryComponent = "..";
		constexpr const char* kInfoDirectoryName = "info";
		constexpr const char* kExcludeFileName = "exclude";
		constexpr const char* kConfigFileName = "config";
		constexpr const char* kWorktreeConfigFileName = "config.worktree";
		constexpr const char* kIgnoreRulesFileName = ".gitignore";
		constexpr const char* kXdgIgnoreFileName = "ignore";
		constexpr const char* kExcludesFileSetting = "core.excludesfile";

		// Compared as components rather than as a string, so that an in-tree file called "..config"
		// is not mistaken for a leading "..".
		bool LeadsOutOfTree(const std::filesystem::path& relativePath)
		{
			const bool hasAComponent = relativePath.begin() != relativePath.end();
			return hasAComponent && *relativePath.begin() == kParentDirectoryComponent;
		}

		bool IsInsideTree(const std::filesystem::path& relativePath)
		{
			return !relativePath.empty() && !LeadsOutOfTree(relativePath);
		}

		std::filesystem::path WorkingDirectoryOf(git_repository* handle)
		{
			const char* workingDirectory = git_repository_workdir(handle);

			if (workingDirectory == nullptr)
			{
				return std::filesystem::path{};
			}

			return FromUtf8(workingDirectory);
		}

		std::optional<std::filesystem::path> ConfiguredExcludesFile(git_repository* handle)
		{
			git_config* snapshot = nullptr;

			if (git_repository_config_snapshot(&snapshot, handle) != 0)
			{
				return std::nullopt;
			}

			git_buf resolved = GIT_BUF_INIT;
			const int lookupResult = git_config_get_path(&resolved, snapshot, kExcludesFileSetting);
			std::optional<std::filesystem::path> excludesFile;

			if (lookupResult == 0 && resolved.ptr != nullptr)
			{
				excludesFile = NormalizePath(FromUtf8(resolved.ptr));
			}

			git_buf_dispose(&resolved);
			git_config_free(snapshot);

			return excludesFile;
		}

		bool NamesSamePath(const std::vector<std::filesystem::path>& files, const std::filesystem::path& wanted)
		{
			for (const std::filesystem::path& file : files)
			{
				if (SamePath(file, wanted))
				{
					return true;
				}
			}

			return false;
		}

		std::vector<std::filesystem::path> IgnoreFilesOnTheXdgSearchPath()
		{
			std::vector<std::filesystem::path> files;
			git_buf searchPath = GIT_BUF_INIT;

			if (git_libgit2_opts(GIT_OPT_GET_SEARCH_PATH, GIT_CONFIG_LEVEL_XDG, &searchPath) == 0 && searchPath.ptr != nullptr)
			{
				std::string_view remaining{searchPath.ptr};

				while (!remaining.empty())
				{
					const std::size_t separator = remaining.find(GIT_PATH_LIST_SEPARATOR);
					const std::string_view directory = remaining.substr(0, separator);

					const std::filesystem::path candidate = NormalizePath(FromUtf8(directory) / kXdgIgnoreFileName);

					if (!directory.empty() && !NamesSamePath(files, candidate))
					{
						files.push_back(candidate);
					}

					remaining = separator == std::string_view::npos ? std::string_view{} : remaining.substr(separator + 1);
				}
			}

			git_buf_dispose(&searchPath);
			return files;
		}

		// WARNING: libgit2 takes core.excludesFile, else the first XDG ignore file, once when it opens the repository.
		std::vector<std::filesystem::path> GlobalExcludesFilesOf(git_repository* handle)
		{
			if (const std::optional<std::filesystem::path> configured = ConfiguredExcludesFile(handle))
			{
				return {*configured};
			}

			return IgnoreFilesOnTheXdgSearchPath();
		}
	}

	Result<Repository> Repository::Open(const std::filesystem::path& anyPathInside)
	{
		const unsigned int noOpenFlags = 0;
		return OpenWith(anyPathInside, noOpenFlags);
	}

	Result<Repository> Repository::OpenExactly(const std::filesystem::path& directory)
	{
		return OpenWith(directory, GIT_REPOSITORY_OPEN_NO_SEARCH);
	}

	Result<Repository> Repository::OpenWith(const std::filesystem::path& where, unsigned int openFlags)
	{
		EnsureLibgit2Initialised();

		git_repository* openedHandle = nullptr;
		const std::string path = ToUtf8(where);

		const char* const searchUpToTheFilesystemRootLikeGit = nullptr;
		const int openResult =
			git_repository_open_ext(&openedHandle, path.c_str(), openFlags, searchUpToTheFilesystemRootLikeGit);

		if (openResult != 0 || openedHandle == nullptr)
		{
			const std::string message = LastLibgit2Message();

			ErrorCode errorCode = ErrorCode::IoFailure;

			if (openResult == GIT_ENOTFOUND)
			{
				errorCode = ErrorCode::NotFound;
			}
			else if (openResult == GIT_EAUTH)
			{
				errorCode = ErrorCode::AccessDenied;
			}

			return Error{errorCode, std::format("cannot open a git repository at '{}': {}", path, message)};
		}

		Repository repository;
		repository.m_Handle.reset(openedHandle);
		repository.m_WorkingDirectory = WorkingDirectoryOf(openedHandle);
		repository.m_WorkingDirectoryKey = PathKey(repository.m_WorkingDirectory);
		repository.m_GlobalExcludesFiles = GlobalExcludesFilesOf(repository.m_Handle.get());

		return repository;
	}

	Result<Repository> Repository::Init(const std::filesystem::path& existingDirectory)
	{
		EnsureLibgit2Initialised();

		git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;

		// MKPATH stays off: the folder is the one open in the IDE, and creating a missing one would
		// hide a wrong path behind a fresh repository.
		const std::uint32_t withoutMakePath = 0;
		options.flags = withoutMakePath;

		git_repository* createdHandle = nullptr;
		const std::string path = ToUtf8(existingDirectory);

		if (git_repository_init_ext(&createdHandle, path.c_str(), &options) != 0 || createdHandle == nullptr)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot initialise a git repository at '{}': {}", path,
									 LastLibgit2Message())};
		}

		Repository repository;
		repository.m_Handle.reset(createdHandle);
		repository.m_WorkingDirectory = WorkingDirectoryOf(createdHandle);
		repository.m_WorkingDirectoryKey = PathKey(repository.m_WorkingDirectory);
		repository.m_GlobalExcludesFiles = GlobalExcludesFilesOf(repository.m_Handle.get());

		return repository;
	}

	void Repository::Free::operator()(git_repository* repository) const
	{
		git_repository_free(repository);
	}

	bool Repository::IsIgnored(const std::filesystem::path& path) const
	{
		if (m_Handle == nullptr)
		{
			return false;
		}

		// WARNING: std::filesystem::relative asks the disk to resolve both paths, which costs as much as the check.
		const std::optional<std::filesystem::path> relativePath = RelativeToKey(path, m_WorkingDirectoryKey);

		if (!relativePath.has_value() || !IsInsideTree(*relativePath))
		{
			return false;
		}

		const std::string gitPath = ToGitPath(*relativePath);

		int ignoredFlag = 0;
		if (git_ignore_path_is_ignored(&ignoredFlag, m_Handle.get(), gitPath.c_str()) != 0)
		{
			CORE_TRACE("Cannot determine ignore status for '{}'", gitPath);
			return false;
		}

		return ignoredFlag != 0;
	}

	// WARNING: A linked worktree reads its exclude file and config from the common directory, not from its own.
	std::vector<std::filesystem::path> Repository::IgnoreRuleFilesOutside(const std::filesystem::path& folder) const
	{
		std::vector<std::filesystem::path> files;

		if (m_Handle == nullptr)
		{
			return files;
		}

		const std::filesystem::path commonDirectory = FromUtf8(git_repository_commondir(m_Handle.get()));
		files.push_back(NormalizePath(commonDirectory / kInfoDirectoryName / kExcludeFileName));
		files.push_back(NormalizePath(commonDirectory / kConfigFileName));
		files.push_back(NormalizePath(FromUtf8(git_repository_path(m_Handle.get())) / kWorktreeConfigFileName));
		files.insert(files.end(), m_GlobalExcludesFiles.begin(), m_GlobalExcludesFiles.end());

		if (m_WorkingDirectory.empty())
		{
			return files;
		}

		const std::filesystem::path normalizedFolder = NormalizeDirectory(folder);

		for (std::filesystem::path above = normalizedFolder.parent_path(); IsSameOrWithin(above, m_WorkingDirectory);
			 above = above.parent_path())
		{
			files.push_back(above / kIgnoreRulesFileName);

			const bool reachedTopOfDrive = above == above.parent_path();

			if (reachedTopOfDrive)
			{
				break;
			}
		}

		return files;
	}

	bool Repository::IsInsideSubmodule(const std::filesystem::path& pathRelativeToWorkingDirectory) const
	{
		if (m_Handle == nullptr || !IsInsideTree(pathRelativeToWorkingDirectory))
		{
			return false;
		}

		Result<IndexHandle> openedIndex = OpenFreshIndex(m_Handle.get());

		if (!openedIndex.HasValue())
		{
			CORE_TRACE("Cannot read the index to look for a submodule above '{}': {}",
					   ToGitPath(pathRelativeToWorkingDirectory), openedIndex.GetError().Message);
			return false;
		}

		const IndexHandle index = std::move(openedIndex).GetValue();

		bool isInsideSubmodule = false;
		std::filesystem::path directoryAbove;

		// Only the directories above the path count: a gitlink at the path itself is the
		// submodule's root directory, not a file inside it.
		for (const std::filesystem::path& component : pathRelativeToWorkingDirectory.parent_path())
		{
			directoryAbove /= component;

			const std::string gitPath = ToGitPath(directoryAbove);
			const git_index_entry* indexEntry = git_index_get_bypath(index.get(), gitPath.c_str(), GIT_INDEX_STAGE_NORMAL);

			if (indexEntry != nullptr && indexEntry->mode == static_cast<std::uint32_t>(GIT_FILEMODE_COMMIT))
			{
				isInsideSubmodule = true;
				break;
			}
		}

		return isInsideSubmodule;
	}
}
