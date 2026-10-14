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
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct InstalledVersion
	{
		std::string Version;
		bool HasChecksum = false;

		bool operator==(const InstalledVersion&) const = default;
	};

	struct InstalledVersionsRead;

	class ServerInstalls
	{
	public:
		ServerInstalls(Core::Ref<Core::HttpClient> http, std::filesystem::path serversDirectory, Core::JobSubmitter submit);

		const std::filesystem::path& GetServersDirectory() const;

		// WARNING: Submitted before any install can be, so a queue with one worker runs it first; what is installed
		// is read on the queue after it, never on the UI thread.
		void SweepAtStartup();

		bool Install(std::string_view serverId);
		bool CheckForUpdates(std::string_view serverId);
		bool Uninstall(std::string_view serverId);
		void Cancel(std::string_view serverId);
		bool IsBusy(std::string_view serverId) const;

		bool CanDownload() const;

		bool HasReadInstalls() const;

		// WARNING: Follows this window's own installs and uninstalls; another window's are seen at the next start.
		std::optional<InstalledVersion> InstalledVersionOf(std::string_view serverId) const;

		std::optional<InstallJobKind> RunningJobOf(std::string_view serverId) const;

		bool IsCancelling(std::string_view serverId) const;

		// WARNING: Call from the UI thread, once a frame.
		void Poll();

		std::optional<InstallProgress> ProgressOf(std::string_view serverId) const;

		void TakeFinished(std::vector<InstallProgress>& finished);

	private:
		using VersionsByServer = std::map<std::string, InstalledVersion, std::less<>>;
		using JobsByServer = std::map<std::string, InstallJobKind, std::less<>>;

		bool NoteStarted(std::string_view serverId, InstallJobKind kind, bool started);
		void AdoptInstalledRead();
		void NoteOutcome(const InstallProgress& outcome);

		Core::Ref<Core::HttpClient> m_Http;
		std::filesystem::path m_ServersDirectory;
		Core::JobSubmitter m_Submit;
		ServerInstaller m_Installer;
		std::map<std::string, InstallProgress, std::less<>> m_Latest;
		std::vector<InstallProgress> m_Finished;
		Core::Ref<InstalledVersionsRead> m_InstalledRead;
		bool m_HaveReadInstalls = false;
		VersionsByServer m_Installed;
		JobsByServer m_Running;
		std::set<std::string, std::less<>> m_Cancelling;
	};

	std::vector<LanguageId> LanguagesServedBy(std::string_view serverId);
}
