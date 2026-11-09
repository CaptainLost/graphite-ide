#pragma once

#include "Core/Base/Timestep.h"
#include "Core/FileSystem/ChangeCollector.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <vector>

namespace Core {

	bool NamesRepositoryState(const std::filesystem::path& pathRelativeToRoot);

	bool NamesIgnoreRules(const std::filesystem::path& pathRelativeToRoot);

	struct ChangeBatch
	{
		std::vector<std::filesystem::path> Directories;

		bool Overflowed = false;

		bool IgnoreRulesChanged = false;

		bool RepositoryStateChanged = false;
	};

	// Turns the paths a watcher saw change under a root into what its users act on: the
	// directories to list again, whether the ignore rules or the repository's state moved, and
	// whether events were lost. Shared by the Windows watcher and the WSL agent's, so both
	// report alike. Not thread-safe: a watcher with a thread of its own locks around it.
	class ChangeBatcher
	{
	public:
		explicit ChangeBatcher(std::filesystem::path root);

		// changedPath is absolute, under the root.
		void Record(const std::filesystem::path& changedPath);

		void RecordOverflow();

		// The same root, spelled another way (the spelling the file system reports): keeps what
		// was collected.
		void Respell(std::filesystem::path sameRoot);

		// The root moved: what was collected is spelled under the old one and is dropped.
		void Rebase(std::filesystem::path newRoot);

		// What settled at least quietPeriod ago; each change is handed over once.
		ChangeBatch TakeSettled(std::chrono::milliseconds quietPeriod);

		std::optional<SteadyClock::time_point> SettlesAt(std::chrono::milliseconds quietPeriod) const;

	private:
		std::filesystem::path m_Root;
		ChangeCollector m_Collector;
		bool m_RepositoryStateChanged = false;
	};
}
