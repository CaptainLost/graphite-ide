#pragma once

#include <filesystem>
#include <optional>

namespace Graphite {

	// The one place that knows where the application keeps the user's files. Nothing of the
	// user's is kept beside the executable: an installed copy lives under Program Files, where
	// it may not write, and one executable can be run by several users.
	//
	// Settings, the window layout and the user's own colour schemes follow the user between
	// machines, so they live under %APPDATA%\Graphite. Caches and logs belong to one machine,
	// so they live under %LOCALAPPDATA%\Graphite. Should Windows not name either folder, both
	// fall back to the executable's directory, which is where they lived before.
	//
	// Every directory returned here, and the parent of every file, exists by the time it is
	// returned.
	class UserDirectories
	{
	public:
		static std::filesystem::path SettingsFile();
		static std::filesystem::path LayoutFile();
		static std::filesystem::path ThemesDirectory();
		static std::filesystem::path SessionsCacheFile();
		static std::filesystem::path LogFile();
		static std::filesystem::path TrustedFoldersFile();
		static std::filesystem::path ServersDirectory();
		static std::filesystem::path ServerTemporaryFilesDirectory();

		// Puts every file under `root` for the rest of the process instead of the user's
		// profile. For a test runner, which must never write the settings of whoever runs it.
		// Must be called before anything above is first asked, and at most once.
		static void Redirect(std::filesystem::path root);

		static std::optional<std::filesystem::path> RedirectedRoot();
	};
}
