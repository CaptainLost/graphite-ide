#include "Settings/UserDirectories.h"

#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/Process.h"

#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kApplicationFolder = "Graphite";
		constexpr const char* kRoamingFolderName = "roaming";
		constexpr const char* kLocalFolderName = "local";

		struct Roots
		{
			std::filesystem::path Roaming;
			std::filesystem::path Local;
		};

		std::optional<std::filesystem::path>& Redirection()
		{
			static std::optional<std::filesystem::path> root;
			return root;
		}

		bool& RootsResolved()
		{
			static bool resolved = false;
			return resolved;
		}

		std::filesystem::path ApplicationFolderIn(const Core::Result<std::filesystem::path>& profileFolder,
												  const char* folderDescription)
		{
			if (profileFolder)
			{
				return profileFolder.GetValue() / kApplicationFolder;
			}

			const Core::Result<std::filesystem::path> executable = Core::ExecutableDirectory();

			if (executable)
			{
				APP_WARN("Cannot locate the {} folder, keeping its files beside the executable: {}",
						 folderDescription, profileFolder.GetError().Message);
				return executable.GetValue();
			}

			APP_WARN("Cannot locate the {} folder or the executable, using the working directory: {}",
					 folderDescription, profileFolder.GetError().Message);
			return std::filesystem::path{"."};
		}

		void EnsureDirectoryExists(const std::filesystem::path& directory)
		{
			std::error_code createError;
			std::filesystem::create_directories(directory, createError);

			if (createError)
			{
				APP_WARN("Cannot create '{}': {}", Core::ToUtf8(directory), createError.message());
			}
		}

		std::filesystem::path EnsuredDirectory(const std::filesystem::path& directory)
		{
			EnsureDirectoryExists(directory);
			return directory;
		}

		Roots ResolveRoots()
		{
			RootsResolved() = true;

			Roots resolved;

			if (const std::optional<std::filesystem::path>& redirectedRoot = Redirection())
			{
				resolved = Roots{*redirectedRoot / kRoamingFolderName, *redirectedRoot / kLocalFolderName};
			}
			else
			{
				resolved = Roots{ApplicationFolderIn(Core::RoamingDataDirectory(), "roaming application data"),
								 ApplicationFolderIn(Core::LocalDataDirectory(), "local application data")};
			}

			EnsureDirectoryExists(resolved.Roaming);
			EnsureDirectoryExists(resolved.Local);
			return resolved;
		}

		const Roots& GetRoots()
		{
			static const Roots roots = ResolveRoots();
			return roots;
		}
	}

	std::filesystem::path UserDirectories::SettingsFile()
	{
		return GetRoots().Roaming / "graphite.json";
	}

	std::filesystem::path UserDirectories::LayoutFile()
	{
		return GetRoots().Roaming / "imgui.ini";
	}

	// WARNING: Not created here: SaveScheme creates it when the first scheme is saved.
	std::filesystem::path UserDirectories::ThemesDirectory()
	{
		return GetRoots().Roaming / "themes";
	}

	std::filesystem::path UserDirectories::SessionsCacheFile()
	{
		return GetRoots().Local / "sessions-cache.json";
	}

	// WARNING: Local, not roaming: the entries are paths on this machine.
	std::filesystem::path UserDirectories::TrustedFoldersFile()
	{
		return GetRoots().Local / "trusted-folders.json";
	}

	// WARNING: Local, not roaming: a server is a binary built for this machine's system and processor.
	std::filesystem::path UserDirectories::ServersDirectory()
	{
		static const std::filesystem::path directory = EnsuredDirectory(GetRoots().Local / "servers");

		return directory;
	}

	// WARNING: Local, not roaming, and not the system's temporary directory: what a killed server leaves here is removed the next time Graphite starts.
	std::filesystem::path UserDirectories::ServerTemporaryFilesDirectory()
	{
		static const std::filesystem::path directory = EnsuredDirectory(GetRoots().Local / "server-temp");

		return directory;
	}

	std::filesystem::path UserDirectories::LogFile()
	{
		static const std::filesystem::path file = EnsuredDirectory(GetRoots().Local / "logs") / "graphite.log";

		return file;
	}

	void UserDirectories::Redirect(std::filesystem::path root)
	{
		CORE_ASSERT(!RootsResolved(), "UserDirectories::Redirect called after the roots were resolved");
		CORE_ASSERT(!Redirection().has_value(), "UserDirectories::Redirect called twice");

		Redirection() = std::move(root);
	}

	std::optional<std::filesystem::path> UserDirectories::RedirectedRoot()
	{
		return Redirection();
	}
}
