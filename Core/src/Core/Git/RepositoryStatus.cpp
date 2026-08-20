#include "corepch.h"

#include "Core/Git/Repository.h"

#include "Core/Fs/FileSystem.h"
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

		git_status_list* list = nullptr;

		if (git_status_list_new(&list, m_Handle, &options) != 0)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot read the working tree status: {}", LastLibgit2Message())};
		}

		WorkingTreeStatus status;
		const std::size_t count = git_status_list_entrycount(list);
		status.Entries.reserve(count);

		for (std::size_t index = 0; index < count; ++index)
		{
			const git_status_entry* entry = git_status_byindex(list, index);

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

			if (delta->new_file.path != nullptr)
			{
				file.Path = FromUtf8(delta->new_file.path);
			}
			else if (delta->old_file.path != nullptr)
			{
				file.Path = FromUtf8(delta->old_file.path);
			}

			if (delta->old_file.path != nullptr && delta->new_file.path != nullptr
				&& std::string_view(delta->old_file.path) != delta->new_file.path)
			{
				file.RenamedFrom = FromUtf8(delta->old_file.path);
			}

			status.Entries.push_back(std::move(file));
		}

		git_status_list_free(list);
		return status;
	}
}
