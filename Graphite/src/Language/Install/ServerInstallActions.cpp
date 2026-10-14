#include "Language/Install/ServerInstallActions.h"

#include "Language/Install/ServerInstalls.h"
#include "Language/Server/LanguageServices.h"

#include "Core/Platform/CurrentPlatform.h"

namespace Graphite {

	namespace {

		bool RunsServer(const LanguageServices& services, LanguageId language, std::string_view serverId)
		{
			const ServerDefinition* chosen = services.ChosenServer(language);

			return chosen != nullptr && chosen->Id == serverId;
		}
	}

	void ReconfigureLanguagesRunning(LanguageServices& services, std::string_view serverId)
	{
		for (const LanguageId language : LanguagesServedBy(serverId))
		{
			if (RunsServer(services, language, serverId))
			{
				services.Reconfigure(language);
			}
		}
	}

	bool UninstallStoppingServers(ServerInstalls& installs, LanguageServices& services, std::string_view serverId)
	{
		// WARNING: The uninstall is started before the stop, so the servers the stop releases find it held back rather than
		// start again.
		if (!installs.Uninstall(serverId))
		{
			return false;
		}

		ReconfigureLanguagesRunning(services, serverId);
		return true;
	}

	void RestartServersAfter(const InstallProgress& outcome, LanguageServices& services)
	{
		const bool placedAVersion = outcome.Step == InstallStep::Installed;
		const bool endedAnUninstall = outcome.Kind == InstallJobKind::Uninstall && IsFinished(outcome.Step);

		if (placedAVersion || endedAnUninstall)
		{
			ReconfigureLanguagesRunning(services, outcome.ServerId);
		}
	}

	bool MayInstall(const ServerInstalls& installs, const ServerDefinition& server)
	{
		return installs.CanDownload() && installs.HasReadInstalls() && !installs.IsBusy(server.Id) &&
			   !installs.InstalledVersionOf(server.Id).has_value() && IsAvailableOn(server.Package, Core::CurrentPlatform());
	}
}
