#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Install/ServerInstaller.h"

#include "Core/Base/Pointers.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Network/HttpClient.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// Without an HTTP client, installs and update checks are refused; uninstalling and the sweep still work.
	class ServerInstalls
	{
	public:
		ServerInstalls(Core::Ref<Core::HttpClient> http, std::filesystem::path serversDirectory, Core::JobSubmitter submit);

		const std::filesystem::path& GetServersDirectory() const;

		// Submitted before any install can be, so a queue with one worker runs it first.
		void SweepAtStartup();

		bool Install(std::string_view serverId);
		bool CheckForUpdates(std::string_view serverId);
		bool Uninstall(std::string_view serverId);
		void Cancel(std::string_view serverId);
		bool IsBusy(std::string_view serverId) const;

		// UI thread, once a frame.
		void Poll();

		std::optional<InstallProgress> ProgressOf(std::string_view serverId) const;

		void TakeFinished(std::vector<InstallProgress>& finished);

	private:
		Core::Ref<Core::HttpClient> m_Http;
		std::filesystem::path m_ServersDirectory;
		Core::JobSubmitter m_Submit;
		ServerInstaller m_Installer;
		std::map<std::string, InstallProgress, std::less<>> m_Latest;
		std::vector<InstallProgress> m_Finished;
	};

	std::vector<LanguageId> LanguagesServedBy(std::string_view serverId);
}
