#include "Language/Server/ServerLaunch.h"

#include "Language/Server/Adapters/ServerAdapter.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/CurrentPlatform.h"

#include <string>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kLogsPlaceholder = "{logs}";

		std::string WithPlacesFilledIn(std::string argument, const LaunchPlaces& places)
		{
			const std::string logDirectory = Core::ToUtf8(places.LogDirectory);

			// Searched past what was put in, so a log directory whose name holds the placeholder cannot loop.
			for (std::size_t placeholder = argument.find(kLogsPlaceholder); placeholder != std::string::npos;
				 placeholder = argument.find(kLogsPlaceholder, placeholder + logDirectory.size()))
			{
				argument.replace(placeholder, kLogsPlaceholder.size(), logDirectory);
			}

			return argument;
		}

		std::string_view ArchitectureDotnetRootName(Core::Architecture machine)
		{
			switch (machine)
			{
			case Core::Architecture::X64:
				return "DOTNET_ROOT_X64";
			case Core::Architecture::Arm64:
				break;
			}

			return "DOTNET_ROOT_ARM64";
		}
	}

	std::string DescribeProgram(const std::filesystem::path& program, std::span<const std::string> leadingArguments)
	{
		std::string description = Core::ToUtf8(program);

		for (const std::string& argument : leadingArguments)
		{
			description += ' ';
			description += argument;
		}

		return description;
	}

	ServerLaunch LaunchFor(const ServerDefinition& server, std::filesystem::path program, const ServerOverrides& overrides,
						   const LaunchPlaces& places)
	{
		std::vector<std::string> arguments = places.LeadingArguments;

		for (std::string& defaultArgument : DefaultArgumentsOf(server))
		{
			arguments.push_back(WithPlacesFilledIn(std::move(defaultArgument), places));
		}

		for (const std::string& extraArgument : overrides.ExtraArguments)
		{
			arguments.push_back(WithPlacesFilledIn(extraArgument, places));
		}

		std::string description = DescribeProgram(program, places.LeadingArguments);

		std::vector<Core::EnvironmentVariable> environment;

		// An .exe server built on .NET looks for its runtime here, not on PATH. The architecture's own variable wins
		// over DOTNET_ROOT, so one inherited from the user's environment would point the server at another runtime.
		if (!places.DotnetRoot.empty())
		{
			const std::string dotnetRoot = Core::ToUtf8(places.DotnetRoot);

			environment.push_back(Core::EnvironmentVariable{.Name = "DOTNET_ROOT", .Value = dotnetRoot});
			environment.push_back(Core::EnvironmentVariable{.Name = std::string{ArchitectureDotnetRootName(Core::CurrentPlatform().Machine)},
															.Value = dotnetRoot});
		}

		return ServerLaunch{.Server = &server,
							.Adapter = &AdapterOf(server.Adapter),
							.Binary = std::move(program),
							.Arguments = std::move(arguments),
							.Description = std::move(description),
							.Environment = std::move(environment),
							.InitializationOptions = overrides.InitializationOptions,
							.Settings = overrides.Settings};
	}
}
