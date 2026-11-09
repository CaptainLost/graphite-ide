#pragma once

#include "Core/Base/Result.h"
#include "Core/FileSystem/ChangeBatcher.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace Agent {

	// Watches a folder and everything below it with inotify, one watch per folder, adding a
	// watch for each folder that appears later. It reports through a Core::ChangeBatcher, so a
	// batch reads exactly as the Windows watcher's does. Single-threaded: the agent's loop calls
	// Poll when the descriptor is readable, and TakeSettled on its ticks.
	class TreeWatch
	{
	public:
		static Core::Result<std::unique_ptr<TreeWatch>> Start(std::filesystem::path root);

		~TreeWatch();

		TreeWatch(const TreeWatch&) = delete;
		TreeWatch& operator=(const TreeWatch&) = delete;

		// Reads whatever inotify has queued, without blocking.
		void Poll();

		int GetDescriptor() const { return m_Descriptor; }

		Core::ChangeBatch TakeSettled();

		// inotify ran out of watches (fs.inotify.max_user_watches) on some folder: changes under
		// it go unreported.
		bool HitWatchLimit() const { return m_HitWatchLimit; }

	private:
		explicit TreeWatch(std::filesystem::path root, int descriptor);

		// Watches folder and every folder below it; when announceEntries, also records each entry
		// found, for a folder that appeared after the watch started and may have filled already.
		void WatchTree(const std::filesystem::path& folder, bool announceEntries);
		void WatchOne(const std::filesystem::path& folder);

		std::filesystem::path m_Root;
		int m_Descriptor = -1;
		std::unordered_map<int, std::filesystem::path> m_Folders;
		Core::ChangeBatcher m_Batcher;
		bool m_HitWatchLimit = false;
	};
}
