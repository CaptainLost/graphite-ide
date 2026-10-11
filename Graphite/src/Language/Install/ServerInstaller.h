#pragma once

#include "Language/Server/ServerCatalog.h"
#include "Language/Server/ServerPackage.h"

#include "Core/Base/Pointers.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Core/Network/HttpClient.h"
#include "Core/Platform/CurrentPlatform.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class InstallStep : std::uint8_t
	{
		Resolving,
		Downloading,
		Verifying,
		Extracting,
		Installed,
		UpToDate,
		UpdateAvailable,
		Uninstalled,
		Cancelled,
		Failed
	};

	enum class InstallJobKind : std::uint8_t
	{
		Install,
		CheckForUpdates,
		Uninstall
	};

	// A job's last change is its outcome: Installed, UpToDate, UpdateAvailable, Uninstalled, Cancelled or Failed.
	struct InstallProgress
	{
		std::string ServerId;
		InstallStep Step = InstallStep::Resolving;
		std::string Version;
		std::uint64_t ReceivedBytes = 0;
		std::optional<std::uint64_t> TotalBytes;
		bool HasChecksum = false;
		std::string Failure;
		InstallJobKind Kind = InstallJobKind::Install;
	};

	bool IsFinished(InstallStep step);

	// A GitHub tag need not sort, so any tag other than the installed one, case aside, counts as newer.
	bool IsNewerRelease(const ServerPackage& package, std::string_view latest, std::string_view installed);

	struct InstallerState;

	class ServerInstaller
	{
	public:
		ServerInstaller(Core::Ref<Core::HttpClient> http, std::filesystem::path serversDirectory,
						Core::PlatformTarget platform, Core::JobSubmitter submit);
		~ServerInstaller();

		ServerInstaller(const ServerInstaller&) = delete;
		ServerInstaller& operator=(const ServerInstaller&) = delete;

		// Each answers false and starts nothing while a job for that server is waiting or running. The definition
		// must outlive the job; the catalogue's rows live for the whole run.
		bool Install(const ServerDefinition& server);
		bool CheckForUpdates(const ServerDefinition& server);
		bool Uninstall(const ServerDefinition& server);

		void Cancel(std::string_view serverId);

		bool IsBusy(std::string_view serverId) const;

		// UI thread. Appends every change since the last call, oldest first.
		void Drain(std::vector<InstallProgress>& changes);

	private:
		bool Start(InstallJobKind kind, const ServerDefinition& server);

		Core::Ref<InstallerState> m_State;
		Core::Ref<Core::HttpClient> m_Http;
		std::filesystem::path m_ServersDirectory;
		Core::PlatformTarget m_Platform;
		Core::JobSubmitter m_Submit;
	};
}
