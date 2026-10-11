#include "Settings/LanguageTabText.h"

#include "UI/TypographicMarks.h"

#include <format>

namespace Graphite {

	std::string ServerOptionLabel(const ServerDefinition& server, bool recommended)
	{
		return std::string{server.DisplayName} + (recommended ? " (recommended)" : "");
	}

	bool RuntimeFound(const ServerRuntime& runtime, std::span<const DotnetInstallation> installations)
	{
		switch (runtime.Kind)
		{
		case RuntimeKind::None:
			return true;
		case RuntimeKind::Dotnet:
			break;
		}

		return FirstWithRuntimeAtLeast(installations, runtime.MinimumMajor) != nullptr;
	}

	std::string RuntimeLine(const ServerRuntime& runtime, std::span<const DotnetInstallation> installations)
	{
		switch (runtime.Kind)
		{
		case RuntimeKind::None:
			return {};
		case RuntimeKind::Dotnet:
			break;
		}

		const std::string needed = std::format(".NET {} runtime", runtime.MinimumMajor) + std::string{kEmDashSeparator};
		const DotnetInstallation* found = FirstWithRuntimeAtLeast(installations, runtime.MinimumMajor);

		if (found == nullptr)
		{
			return needed + "not found";
		}

		// Highest first, and this installation has one at or above the major needed, so the first is the one that runs.
		return needed + "found " + found->RuntimeVersions.front();
	}
}
