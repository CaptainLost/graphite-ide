#include "Settings/UserDirectories.h"

#include "Core/Base/Assert.h"
#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Platform/Process.h"

#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kApplicationFolder = "Graphite";

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

		std::filesystem::path Beside(const Core::Result<std::filesystem::path>& profileFolder,
									 const char* what)
		{
			if (profileFolder)
				return profileFolder.GetValue() / kApplicationFolder;

			// Falling back rather than failing keeps the application usable, the way it was
			// before these folders were used; the warning is what explains settings that
			// silently stayed beside the executable.
			const auto executable = Core::ExecutableDirectory();

			if (executable)
			{
				APP_WARN("Cannot locate the {} folder, keeping its files beside the executable: {}",
						 what, profileFolder.GetError().Message);
				return executable.GetValue();
			}

			APP_WARN("Cannot locate the {} folder or the executable, using the working directory: {}",
					 what, profileFolder.GetError().Message);
			return std::filesystem::path{"."};
		}

		void Ensure(const std::filesystem::path& directory)
		{
			std::error_code code;
			std::filesystem::create_directories(directory, code);

			if (code)
				APP_WARN("Cannot create '{}': {}", Core::ToUtf8(directory), code.message());
		}

		const Roots& GetRoots()
		{
			static const Roots roots = [] {
				RootsResolved() = true;

				Roots resolved;

				if (const auto& redirected = Redirection())
				{
					resolved = Roots{*redirected / "roaming", *redirected / "local"};
				}
				else
				{
					resolved = Roots{Beside(Core::RoamingDataDirectory(), "roaming application data"),
									 Beside(Core::LocalDataDirectory(), "local application data")};
				}

				Ensure(resolved.Roaming);
				Ensure(resolved.Local);
				return resolved;
			}();

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

	// Not created here: SaveScheme creates it when the first scheme is saved, and a user who
	// never makes one never gets an empty directory.
	std::filesystem::path UserDirectories::ThemesDirectory()
	{
		return GetRoots().Roaming / "themes";
	}

	std::filesystem::path UserDirectories::SessionsCacheFile()
	{
		return GetRoots().Local / "sessions-cache.json";
	}

	// Local, not roaming: the entries are paths on this machine, and trusting D:\Repos here says
	// nothing about whatever D:\Repos is on another one.
	std::filesystem::path UserDirectories::TrustedFoldersFile()
	{
		return GetRoots().Local / "trusted-folders.json";
	}

	std::filesystem::path UserDirectories::LogFile()
	{
		static const std::filesystem::path file = [] {
			const std::filesystem::path logs = GetRoots().Local / "logs";
			Ensure(logs);
			return logs / "graphite.log";
		}();

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
