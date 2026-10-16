#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <string>
#include <string_view>

namespace Core {

	namespace {

		constexpr unsigned int kListChangesWithRenames =
			GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX |
			GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR | GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY;

		// Without RECURSE_UNTRACKED_DIRS libgit2 answers with the directory itself, the entry the
		// caller already has. DISABLE_PATHSPEC_MATCH stops the pathspec being read as a glob, where
		// the '[' and ']' a Windows directory name may contain would form a character class.
		constexpr unsigned int kListUntrackedFilesInsideDirectory =
			GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
			GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY | GIT_STATUS_OPT_DISABLE_PATHSPEC_MATCH;

		// libgit2 names a collapsed untracked directory with a trailing '/'. It is cut off the path,
		// so filename() names the directory instead of answering empty.
		constexpr std::string_view kCollapsedDirectorySuffix = "/";

		StatusFlags IndexFlagsOf(unsigned int libgit2Status)
		{
			StatusFlags flags = StatusFlags::None;

			if ((libgit2Status & GIT_STATUS_INDEX_NEW) != 0)
			{
				flags |= StatusFlags::New;
			}
			if ((libgit2Status & GIT_STATUS_INDEX_MODIFIED) != 0)
			{
				flags |= StatusFlags::Modified;
			}
			if ((libgit2Status & GIT_STATUS_INDEX_DELETED) != 0)
			{
				flags |= StatusFlags::Deleted;
			}
			if ((libgit2Status & GIT_STATUS_INDEX_RENAMED) != 0)
			{
				flags |= StatusFlags::Renamed;
			}
			if ((libgit2Status & GIT_STATUS_INDEX_TYPECHANGE) != 0)
			{
				flags |= StatusFlags::TypeChanged;
			}

			return flags;
		}

		StatusFlags WorkTreeFlagsOf(unsigned int libgit2Status)
		{
			StatusFlags flags = StatusFlags::None;

			if ((libgit2Status & GIT_STATUS_WT_NEW) != 0)
			{
				flags |= StatusFlags::New;
			}
			if ((libgit2Status & GIT_STATUS_WT_MODIFIED) != 0)
			{
				flags |= StatusFlags::Modified;
			}
			if ((libgit2Status & GIT_STATUS_WT_DELETED) != 0)
			{
				flags |= StatusFlags::Deleted;
			}
			if ((libgit2Status & GIT_STATUS_WT_RENAMED) != 0)
			{
				flags |= StatusFlags::Renamed;
			}
			if ((libgit2Status & GIT_STATUS_WT_TYPECHANGE) != 0)
			{
				flags |= StatusFlags::TypeChanged;
			}

			return flags;
		}
	}

	Result<WorkingTreeStatus> Repository::QueryStatus() const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		git_status_options options = GIT_STATUS_OPTIONS_INIT;
		options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
		options.flags = kListChangesWithRenames;

		StatusListHandle statusList;

		if (git_status_list_new(Out(statusList), m_Handle.get(), &options) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the working tree status: {}", LastLibgit2Message())};
		}

		WorkingTreeStatus workingTreeStatus;
		const std::size_t entryCount = git_status_list_entrycount(statusList.get());
		workingTreeStatus.Entries.reserve(entryCount);

		for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
		{
			const git_status_entry* entry = git_status_byindex(statusList.get(), entryIndex);

			if (entry == nullptr || entry->status == GIT_STATUS_CURRENT)
			{
				continue;
			}

			const bool isNewInWorkTree = (entry->status & GIT_STATUS_WT_NEW) != 0;
			const bool isNewInIndex = (entry->status & GIT_STATUS_INDEX_NEW) != 0;

			FileStatus file;
			file.Index = IndexFlagsOf(entry->status);
			file.WorkTree = WorkTreeFlagsOf(entry->status);
			file.IsConflicted = (entry->status & GIT_STATUS_CONFLICTED) != 0;
			file.IsUntracked = isNewInWorkTree && !isNewInIndex;

			// index_to_workdir names the file as it is on disk now; head_to_index names it only when
			// the working tree has nothing to say, such as a staged deletion.
			const git_diff_delta* delta =
				entry->index_to_workdir != nullptr ? entry->index_to_workdir : entry->head_to_index;

			if (delta == nullptr)
			{
				continue;
			}

			std::string_view reportedPath;

			if (delta->new_file.path != nullptr)
			{
				reportedPath = delta->new_file.path;
			}
			else if (delta->old_file.path != nullptr)
			{
				reportedPath = delta->old_file.path;
			}

			if (reportedPath.ends_with(kCollapsedDirectorySuffix))
			{
				file.IsDirectory = true;
				reportedPath.remove_suffix(kCollapsedDirectorySuffix.size());
			}

			file.Path = FromUtf8(reportedPath);

			const bool hasBothPaths = delta->old_file.path != nullptr && delta->new_file.path != nullptr;

			if (hasBothPaths && std::string_view(delta->old_file.path) != delta->new_file.path)
			{
				file.RenamedFrom = FromUtf8(delta->old_file.path);
			}

			workingTreeStatus.Entries.push_back(std::move(file));
		}

		return workingTreeStatus;
	}

	Result<WorkingTreeStatus> Repository::QueryUntrackedIn(const std::filesystem::path& directory) const
	{
		if (m_Handle == nullptr)
		{
			return RepositoryNotOpen();
		}

		const PathSpec directorySpec{{directory}};

		git_status_options options = GIT_STATUS_OPTIONS_INIT;
		options.show = GIT_STATUS_SHOW_WORKDIR_ONLY;
		options.flags = kListUntrackedFilesInsideDirectory;
		options.pathspec = directorySpec.Get();

		StatusListHandle statusList;

		if (git_status_list_new(Out(statusList), m_Handle.get(), &options) != 0)
		{
			return Error{ErrorCode::IoFailure, std::format("cannot list the untracked files in '{}': {}",
														   ToGitPath(directory), LastLibgit2Message())};
		}

		WorkingTreeStatus workingTreeStatus;
		const std::size_t entryCount = git_status_list_entrycount(statusList.get());

		for (std::size_t entryIndex = 0; entryIndex < entryCount; ++entryIndex)
		{
			const git_status_entry* entry = git_status_byindex(statusList.get(), entryIndex);

			if (entry == nullptr || (entry->status & GIT_STATUS_WT_NEW) == 0)
			{
				continue;
			}

			const git_diff_delta* delta = entry->index_to_workdir;

			if (delta == nullptr || delta->new_file.path == nullptr)
			{
				continue;
			}

			FileStatus file;
			file.WorkTree = StatusFlags::New;
			file.IsUntracked = true;
			file.Path = FromUtf8(delta->new_file.path);

			workingTreeStatus.Entries.push_back(std::move(file));
		}

		return workingTreeStatus;
	}
}
