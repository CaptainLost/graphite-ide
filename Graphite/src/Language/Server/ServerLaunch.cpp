#include "Language/Server/ServerLaunch.h"

#include "Language/Server/Adapters/ServerAdapter.h"

#include "Core/FileSystem/FileSystem.h"

#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kLogsPlaceholder = "{logs}";

		std::string WithPlacesFilledIn(std::string argument, const LaunchPlaces& places)
		{
			if (const std::size_t placeholder = argument.find(kLogsPlaceholder); placeholder != std::string::npos)
			{
				argument.replace(placeholder, kLogsPlaceholder.size(), Core::ToUtf8(places.LogDirectory));
			}

			return argument;
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

		return ServerLaunch{.Server = &server,
							.Adapter = &AdapterOf(server.Adapter),
							.Binary = std::move(program),
							.Arguments = std::move(arguments),
							.Description = std::move(description),
							.InitializationOptions = overrides.InitializationOptions,
							.Settings = overrides.Settings};
	}
}
