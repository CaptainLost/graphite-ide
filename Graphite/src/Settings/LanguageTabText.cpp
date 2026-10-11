#include "Settings/LanguageTabText.h"

#include "UI/TypographicMarks.h"

#include <format>

namespace Graphite {

	std::string ServerOptionLabel(const ServerDefinition& server, bool recommended)
	{
		return std::string{server.DisplayName} + (recommended ? " (recommended)" : "");
	}

	std::string ChoicePreview(LanguageId language, const ServerChoice& choice)
	{
		switch (choice.Kind)
		{
		case ServerChoiceKind::NotConfigured:
			return "Not chosen";
		case ServerChoiceKind::Off:
			return "None";
		case ServerChoiceKind::Chosen:
			break;
		}

		return ServerOptionLabel(*choice.Server, choice.Server == RecommendedServerFor(language));
	}

	const char* InstallActionLabel(InstallAction action)
	{
		switch (action)
		{
		case InstallAction::Install:
			return "Install";
		case InstallAction::Cancel:
			return "Cancel";
		case InstallAction::CheckForUpdates:
			return "Check for updates";
		case InstallAction::Update:
			return "Update";
		case InstallAction::Uninstall:
			break;
		}

		return "Uninstall";
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
