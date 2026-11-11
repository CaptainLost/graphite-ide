#include "Wsl/WslServerLaunch.h"

#include "Wsl/WslServerUris.h"

#include "Core/FileSystem/FileSystem.h"

#include <format>
#include <utility>

namespace Graphite {

	namespace {

		// Where a server writes its logs in the distro, for an argument that names a log folder.
		constexpr std::string_view kDistroLogDirectory = "/tmp/graphite-language-servers";

		// The script the login shell runs: the program, with every argument as it was given.
		constexpr std::string_view kRunProgram = "exec \"$0\" \"$@\"";
	}

	std::string DistroServerProgram(const ServerDefinition& server, const std::filesystem::path& configured)
	{
		const std::string path = Core::ToGenericUtf8(configured);

		if (path.starts_with('/') && !path.starts_with("//"))
		{
			return path;
		}

		return std::string{server.ExecutableName};
	}

	ServerLaunch WslServerLaunch(const ServerDefinition& server, const std::string& program, const ServerOverrides& overrides,
								 const Core::WslSharePath& root, const std::filesystem::path& wslExecutable)
	{
		// No temporary directory or .NET root: their environment variables would name Windows folders.
		ServerLaunch launch = LaunchFor(server, Core::FromUtf8(program), overrides, LaunchPlaces{.LogDirectory = Core::FromUtf8(kDistroLogDirectory)});

		std::vector<std::string> arguments{"-d", root.Distro, "--cd", root.LinuxPath, "--exec", "/bin/sh", "-lc", std::string{kRunProgram}, program};
		arguments.insert(arguments.end(), std::make_move_iterator(launch.Arguments.begin()), std::make_move_iterator(launch.Arguments.end()));

		launch.Binary = wslExecutable;
		launch.Arguments = std::move(arguments);
		launch.Environment.clear();
		launch.Description = std::format("{} in {}", program, root.Distro);
		launch.WhenProgramMissing = std::format("{} is not installed in {}, or not on its PATH", program, root.Distro);
		launch.Translation = BodyTranslation{.ToServer = [root](std::string_view body) { return ToDistroBody(body, root); },
											 .FromServer = [distro = root.Distro](std::string_view body) { return FromDistroBody(body, distro); }};
		return launch;
	}
}
