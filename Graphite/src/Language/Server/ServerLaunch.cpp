#include "Language/Server/ServerLaunch.h"

#include "Language/Server/Adapters/ServerAdapter.h"

#include <utility>

namespace Graphite {

	ServerLaunch LaunchFor(const ServerDefinition& server, std::filesystem::path binary, const ServerOverrides& overrides)
	{
		std::vector<std::string> arguments = DefaultArgumentsOf(server);

		for (const std::string& extraArgument : overrides.ExtraArguments)
		{
			arguments.push_back(extraArgument);
		}

		return ServerLaunch{.Server = &server,
							.Adapter = &AdapterOf(server.Adapter),
							.Binary = std::move(binary),
							.Arguments = std::move(arguments),
							.InitializationOptions = overrides.InitializationOptions,
							.Settings = overrides.Settings};
	}
}
