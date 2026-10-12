#include "Settings/LanguageServers/LanguageTabText.h"

#include "Editor/StatusBar/ServerStatusText.h"
#include "Language/Registry/LanguageRegistry.h"
#include "UI/TypographicMarks.h"

#include <format>
#include <utility>

namespace Graphite {

	namespace {

		bool IsPathProblem(ServerState state)
		{
			switch (state)
			{
			case ServerState::NotFound:
			case ServerState::Failed:
				return true;
			case ServerState::NotConfigured:
			case ServerState::Off:
			case ServerState::NotInstalled:
			case ServerState::MissingRuntime:
			case ServerState::Starting:
			case ServerState::Indexing:
			case ServerState::ProjectIncomplete:
			case ServerState::Ready:
			case ServerState::Restricted:
				break;
			}

			return false;
		}
	}

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

	const char* CustomChoiceLabel()
	{
		return "Custom";
	}

	// The same inputs SummariseInstall reads as Not installed; the two change together.
	bool IsFoundOnPath(const InstallInputs& inputs, const std::optional<ServerCommand>& found, const std::filesystem::path& customPath)
	{
		const bool readsNotInstalled = inputs.AvailableOnPlatform && inputs.HasReadInstalls && !inputs.Running.has_value() && !inputs.Installed.has_value();
		const bool runsFromOutsideInstall = found.has_value() && found->InstalledFile.empty();
		const bool customPathSet = !customPath.empty();

		return readsNotInstalled && runsFromOutsideInstall && !customPathSet;
	}

	const char* FoundOnPathTitle()
	{
		return "Found on PATH";
	}

	std::string FoundOnPathNote(const ServerCommand& command)
	{
		return std::format("In use: {}. Installing puts Graphite's copy ahead of it.", DescribeCommand(command));
	}

	const char* FoundOnPathActionLabel(InstallAction action)
	{
		switch (action)
		{
		case InstallAction::Install:
			return "Install Graphite's copy";
		case InstallAction::Cancel:
		case InstallAction::CheckForUpdates:
		case InstallAction::Update:
		case InstallAction::Uninstall:
			break;
		}

		return InstallActionLabel(action);
	}

	PathRowText CustomPathRowText(LanguageId language, const std::filesystem::path& storedPath, const ServerStatus& status)
	{
		if (storedPath.empty())
		{
			return PathRowText{.Description = "Choose the program to run."};
		}

		const bool notStartedYet = status.State == ServerState::NotConfigured;

		if (notStartedYet)
		{
			return PathRowText{.Description = std::format("Starts when a {} file is opened.", LanguageDisplayName(language))};
		}

		std::string detail = ServerStatusDetail(status);

		if (IsPathProblem(status.State))
		{
			return PathRowText{.Problem = std::move(detail)};
		}

		return PathRowText{.Description = std::move(detail)};
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
