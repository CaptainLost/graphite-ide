#include "corepch.h"

#include "Watching/TreeWatch.h"

#include "Core/FileSystem/DirectoryWatcher.h"

#include <sys/inotify.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <format>
#include <system_error>
#include <vector>

namespace Agent {

	namespace {

		constexpr std::uint32_t kFolderEvents = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE |
												IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;

		// Several events' worth: inotify returns whole events only, and a name is at most NAME_MAX.
		constexpr std::size_t kReadBufferBytes = 64 * 1024;
	}

	Core::Result<std::unique_ptr<TreeWatch>> TreeWatch::Start(std::filesystem::path root)
	{
		std::error_code error;

		if (!std::filesystem::is_directory(root, error))
		{
			return Core::Error{Core::ErrorCode::NotFound, std::format("'{}' is not a folder", root.string())}; // not a path conversion
		}

		const int descriptor = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

		if (descriptor < 0)
		{
			return Core::Error{Core::ErrorCode::IoFailure,
							   std::format("cannot start inotify: {}", std::error_code{errno, std::generic_category()}.message())};
		}

		std::unique_ptr<TreeWatch> watch{new TreeWatch{std::move(root), descriptor}};
		watch->WatchTree(watch->m_Root, false);
		return watch;
	}

	TreeWatch::TreeWatch(std::filesystem::path root, int descriptor)
		: m_Root(std::move(root)), m_Descriptor(descriptor), m_Batcher(m_Root)
	{
	}

	TreeWatch::~TreeWatch()
	{
		if (m_Descriptor >= 0)
		{
			::close(m_Descriptor);
		}
	}

	void TreeWatch::WatchOne(const std::filesystem::path& folder)
	{
		const int watchDescriptor = ::inotify_add_watch(m_Descriptor, folder.c_str(), kFolderEvents);

		if (watchDescriptor >= 0)
		{
			m_Folders[watchDescriptor] = folder;
			return;
		}

		if (errno == ENOSPC && !m_HitWatchLimit)
		{
			// Changes below this folder will go unseen; a reader is told the tree is not whole.
			m_HitWatchLimit = true;
			m_Batcher.RecordOverflow();
		}
	}

	void TreeWatch::WatchTree(const std::filesystem::path& folder, bool announceEntries)
	{
		// Walked folder by folder, not with recursive_directory_iterator: that one ends the whole
		// walk at the first error other than a denied permission, and a folder a build deletes
		// between being listed and being entered would leave every folder after it unwatched.
		// Here such a folder is skipped and the walk goes on.
		std::vector<std::filesystem::path> pending{folder};

		while (!pending.empty())
		{
			const std::filesystem::path current = std::move(pending.back());
			pending.pop_back();

			WatchOne(current);

			std::error_code error;
			std::filesystem::directory_iterator entries{current, std::filesystem::directory_options::skip_permission_denied, error};

			for (; !error && entries != std::filesystem::directory_iterator{}; entries.increment(error))
			{
				const std::filesystem::directory_entry& entry = *entries;

				if (announceEntries)
				{
					m_Batcher.Record(entry.path());
				}

				std::error_code typeError;
				const bool isFolder = entry.is_directory(typeError) && !entry.is_symlink(typeError);

				if (isFolder)
				{
					pending.push_back(entry.path());
				}
			}
		}
	}

	void TreeWatch::Poll()
	{
		alignas(inotify_event) std::array<char, kReadBufferBytes> buffer{};

		while (true)
		{
			const ssize_t bytesRead = ::read(m_Descriptor, buffer.data(), buffer.size());

			if (bytesRead <= 0)
			{
				return; // EAGAIN: nothing more queued
			}

			for (ssize_t offset = 0; offset < bytesRead;)
			{
				const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
				offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);

				if ((event->mask & IN_Q_OVERFLOW) != 0)
				{
					m_Batcher.RecordOverflow();
					continue;
				}

				const auto folder = m_Folders.find(event->wd);

				if (folder == m_Folders.end())
				{
					continue;
				}

				if ((event->mask & IN_IGNORED) != 0)
				{
					m_Folders.erase(folder);
					continue;
				}

				if ((event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0)
				{
					// The folder itself went: its parent's listing changed, which the parent's
					// own event reports; the root going is reported as the root.
					m_Batcher.Record(folder->second);
					continue;
				}

				const std::filesystem::path changed = event->len > 0 ? folder->second / event->name : folder->second;
				m_Batcher.Record(changed);

				const bool aFolderAppeared = (event->mask & IN_ISDIR) != 0 && (event->mask & (IN_CREATE | IN_MOVED_TO)) != 0;

				if (aFolderAppeared)
				{
					// Between mkdir and this watch the folder may have filled; what is in it now
					// is recorded, so nothing made in that gap is lost.
					WatchTree(changed, true);
				}
			}
		}
	}

	Core::ChangeBatch TreeWatch::TakeSettled()
	{
		return m_Batcher.TakeSettled(Core::DirectoryWatcher::kQuietPeriod);
	}
}
