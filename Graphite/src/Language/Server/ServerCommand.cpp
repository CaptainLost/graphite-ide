#include "Language/Server/ServerCommand.h"

#include "Language/Install/InstalledServer.h"
#include "Language/Install/ToolSettings.h"
#include "Language/Server/ServerDiscovery.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <format>
#include <optional>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {

		bool IsRegularFile(const std::filesystem::path& path)
		{
			std::error_code failure;
			return std::filesystem::is_regular_file(path, failure) && !failure;
		}

		bool IsDotnetAssembly(const std::filesystem::path& path)
		{
			return Core::LoweredExtension(path) == ".dll";
		}

		bool NeedsDotnet(const ServerDefinition& server, LaunchRunner runner)
		{
			return server.Runtime.Kind == RuntimeKind::Dotnet || runner == LaunchRunner::Dotnet;
		}

		ServerCommand Unavailable(ServerState state, std::string detail)
		{
			return ServerCommand{.State = state, .Detail = std::move(detail)};
		}

		ServerCommand CommandFor(const ServerDefinition& server, const std::filesystem::path& file, LaunchRunner runner,
								 std::filesystem::path installedFile, const ServerCommandPlaces& places)
		{
			ServerCommand command{.Program = file, .InstalledFile = std::move(installedFile)};

			if (!NeedsDotnet(server, runner))
			{
				return command;
			}

			// Checked before starting: a .NET server without its runtime exits at once, and a server that keeps
			// dying says nothing about why.
			const DotnetInstallation* dotnet = FirstWithRuntimeAtLeast(places.Dotnet, server.Runtime.MinimumMajor);

			if (dotnet == nullptr)
			{
				return Unavailable(ServerState::MissingRuntime, std::format("needs .NET {}", server.Runtime.MinimumMajor));
			}

			if (runner == LaunchRunner::Dotnet)
			{
				command.Program = dotnet->Executable;
				command.LeadingArguments.push_back(Core::ToUtf8(file));
			}

			return command;
		}
	}

	ServerCommand FindServerCommand(const ServerDefinition& server, const std::filesystem::path& configured,
									const ServerCommandPlaces& places)
	{
		if (!configured.empty())
		{
			if (!IsRegularFile(configured))
			{
				return Unavailable(ServerState::NotFound,
								   std::format("the configured path '{}' does not exist", Core::ToUtf8(configured)));
			}

			const LaunchRunner runner = IsDotnetAssembly(configured) ? LaunchRunner::Dotnet : LaunchRunner::Executable;
			return CommandFor(server, configured, runner, {}, places);
		}

		if (!places.ServersDirectory.empty())
		{
			if (const std::optional<InstalledServer> installed = FindInstalledServer(ServerDirectoryOf(places.ServersDirectory, server.Id)))
			{
				return CommandFor(server, installed->Command, installed->Record.Command.Runner, installed->Command, places);
			}
		}

		const std::filesystem::path noConfiguredPath;
		const Core::Result<std::filesystem::path> found = FindServerBinary(server.ExecutableName, noConfiguredPath);

		if (found)
		{
			return CommandFor(server, found.GetValue(), LaunchRunner::Executable, {}, places);
		}

		if (places.ServersDirectory.empty())
		{
			return Unavailable(ServerState::NotFound, "it is neither beside the application nor on PATH");
		}

		return Unavailable(ServerState::NotInstalled, "it is not in Graphite's servers folder, beside the application or on PATH");
	}

	std::string DescribeCommand(const ServerCommand& command)
	{
		std::string description = Core::ToUtf8(command.Program);

		for (const std::string& argument : command.LeadingArguments)
		{
			description += ' ';
			description += argument;
		}

		return description;
	}
}
