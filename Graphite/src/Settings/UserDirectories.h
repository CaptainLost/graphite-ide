#pragma once

#include <filesystem>
#include <optional>

namespace Graphite {

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
		static std::filesystem::path GitRepositoriesDirectory();
		static std::filesystem::path UnsavedCopiesDirectory();

		static void Redirect(std::filesystem::path root);

		static std::optional<std::filesystem::path> RedirectedRoot();
	};
}
