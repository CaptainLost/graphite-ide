#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Git/GitHandles.h"
#include "Core/Git/Libgit2Support.h"

#include <git2.h>

#include <format>
#include <string>
#include <string_view>

namespace Core {

	namespace {

		StatusFlags IndexFlagsOf(unsigned int status)
		{
			StatusFlags flags = StatusFlags::None;

			if ((status & GIT_STATUS_INDEX_NEW) != 0) { flags |= StatusFlags::New; }
			if ((status & GIT_STATUS_INDEX_MODIFIED) != 0) { flags |= StatusFlags::Modified; }
			if ((status & GIT_STATUS_INDEX_DELETED) != 0) { flags |= StatusFlags::Deleted; }
			if ((status & GIT_STATUS_INDEX_RENAMED) != 0) { flags |= StatusFlags::Renamed; }
			if ((status & GIT_STATUS_INDEX_TYPECHANGE) != 0) { flags |= StatusFlags::TypeChanged; }

			return flags;
		}

		StatusFlags WorkTreeFlagsOf(unsigned int status)
		{
			StatusFlags flags = StatusFlags::None;

			if ((status & GIT_STATUS_WT_NEW) != 0) { flags |= StatusFlags::New; }
			if ((status & GIT_STATUS_WT_MODIFIED) != 0) { flags |= StatusFlags::Modified; }
			if ((status & GIT_STATUS_WT_DELETED) != 0) { flags |= StatusFlags::Deleted; }
			if ((status & GIT_STATUS_WT_RENAMED) != 0) { flags |= StatusFlags::Renamed; }
			if ((status & GIT_STATUS_WT_TYPECHANGE) != 0) { flags |= StatusFlags::TypeChanged; }

			return flags;
		}
	}

	Result<WorkingTreeStatus> Repository::QueryStatus() const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		git_status_options options = GIT_STATUS_OPTIONS_INIT;
		options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
		options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
						| GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
						| GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR
						| GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY;

		StatusListHandle list;

		if (git_status_list_new(Out(list), m_Handle.get(), &options) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the working tree status: {}", LastLibgit2Message())};
		}

		WorkingTreeStatus status;
		const std::size_t count = git_status_list_entrycount(list.get());
		status.Entries.reserve(count);

		for (std::size_t index = 0; index < count; ++index)
		{
			const git_status_entry* entry = git_status_byindex(list.get(), index);

			if (entry == nullptr || entry->status == GIT_STATUS_CURRENT)
			{
				continue;
			}

			FileStatus file;
			file.Index = IndexFlagsOf(entry->status);
			file.WorkTree = WorkTreeFlagsOf(entry->status);
			file.IsConflicted = (entry->status & GIT_STATUS_CONFLICTED) != 0;
			file.IsUntracked = (entry->status & GIT_STATUS_WT_NEW) != 0
							   && (entry->status & GIT_STATUS_INDEX_NEW) == 0;

			// index_to_workdir names the file as it is on disk now; head_to_index names it
			// only when the working tree has nothing to say, such as a staged deletion.
			const git_diff_delta* delta = entry->index_to_workdir != nullptr
											  ? entry->index_to_workdir
											  : entry->head_to_index;

			if (delta == nullptr)
			{
				continue;
			}

			std::string_view reported;

			if (delta->new_file.path != nullptr)
			{
				reported = delta->new_file.path;
			}
			else if (delta->old_file.path != nullptr)
			{
				reported = delta->old_file.path;
			}

			// libgit2 names a collapsed untracked directory with a trailing '/'. Kept as a
			// flag and cut off the path, so filename() names the directory instead of
			// answering empty and pushing the name into parent_path().
			if (reported.ends_with('/'))
			{
				file.IsDirectory = true;
				reported.remove_suffix(1);
			}

			file.Path = FromUtf8(reported);

			if (delta->old_file.path != nullptr && delta->new_file.path != nullptr
				&& std::string_view(delta->old_file.path) != delta->new_file.path)
			{
				file.RenamedFrom = FromUtf8(delta->old_file.path);
			}

			status.Entries.push_back(std::move(file));
		}

		return status;
	}

	Result<WorkingTreeStatus> Repository::QueryUntrackedIn(
		const std::filesystem::path& directory) const
	{
		if (m_Handle == nullptr)
		{
			return Error{ErrorCode::NotFound, "the repository is not open"};
		}

		const PathSpec spec{{directory}};

		git_status_options options = GIT_STATUS_OPTIONS_INIT;
		options.show = GIT_STATUS_SHOW_WORKDIR_ONLY;

		// RECURSE_UNTRACKED_DIRS is the whole point: without it libgit2 answers with the
		// directory again, which is the entry the caller already has.
		//
		// DISABLE_PATHSPEC_MATCH is needed because the pathspec is otherwise read as an
		// fnmatch-style glob: a directory whose name contains '[' or ']' — legal on Windows,
		// where '*' and '?' are not — would have its brackets read as a character class and
		// match nothing.
		options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
						| GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
						| GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY
						| GIT_STATUS_OPT_DISABLE_PATHSPEC_MATCH;

		options.pathspec = spec.Get();

		StatusListHandle list;

		if (git_status_list_new(Out(list), m_Handle.get(), &options) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot list the untracked files in '{}': {}", ToGitPath(directory),
									 LastLibgit2Message())};
		}

		WorkingTreeStatus status;
		const std::size_t count = git_status_list_entrycount(list.get());

		for (std::size_t index = 0; index < count; ++index)
		{
			const git_status_entry* entry = git_status_byindex(list.get(), index);

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

			status.Entries.push_back(std::move(file));
		}

		return status;
	}
}
