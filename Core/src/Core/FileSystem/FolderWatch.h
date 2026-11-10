#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace Core {

	struct FolderChanges
	{
		std::vector<std::filesystem::path> Directories;

		bool Overflowed = false;

		bool IgnoreRulesChanged = false;

		bool RepositoryStateChanged = false;

		std::optional<std::filesystem::path> MovedTo;
	};

	// What watches a folder for whoever shows it: the Windows watcher on a local folder, the WSL
	// agent's inotify watch on a folder inside WSL (ADR 0696).
	class FolderWatch
	{
	public:
		virtual ~FolderWatch() = default;

		// What settled since the last call; each change is handed over once.
		virtual FolderChanges TakeSettled() = 0;

		// False once the watch has stopped for good and changes go unreported.
		virtual bool IsRunning() const = 0;
	};
}
