#include "Language/Install/ServerInstalls.h"

#include "Language/Install/InstalledServer.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerCatalog.h"

#include "Core/App/Redraw.h"
#include "Core/Platform/CurrentPlatform.h"

#include <iterator>
#include <mutex>
#include <utility>

namespace Graphite {

	struct InstalledVersionsRead
	{
		std::mutex Mutex;
		std::optional<std::map<std::string, InstalledVersion, std::less<>>> Found;
	};

	namespace {

		bool ListsServer(const LanguageDefinition& definition, std::string_view serverId)
		{
			for (const std::string_view listed : definition.Servers)
			{
				if (listed == serverId)
				{
					return true;
				}
			}

			return false;
		}

		void ReadInstalledVersions(const std::filesystem::path& serversDirectory, InstalledVersionsRead& read)
		{
			std::map<std::string, InstalledVersion, std::less<>> found;

			for (const ServerDefinition& server : AllServers())
			{
				const std::optional<InstalledServer> installed = FindInstalledServer(ServerDirectoryOf(serversDirectory, server.Id));

				if (installed)
				{
					found.emplace(std::string{server.Id}, InstalledVersion{.Version = installed->Record.Version,
																		   .HasChecksum = installed->Record.VerifiedChecksum.has_value()});
				}
			}

			{
				const std::scoped_lock lock{read.Mutex};
				read.Found = std::move(found);
			}

			Core::Redraw::Request();
		}
	}

	ServerInstalls::ServerInstalls(Core::Ref<Core::HttpClient> http, std::filesystem::path serversDirectory, Core::JobSubmitter submit)
		: m_Http(std::move(http)),
		  m_ServersDirectory(std::move(serversDirectory)),
		  m_Submit(std::move(submit)),
		  m_Installer(m_Http, m_ServersDirectory, Core::CurrentPlatform(), m_Submit),
		  m_InstalledRead(Core::CreateRef<InstalledVersionsRead>())
	{
	}

	const std::filesystem::path& ServerInstalls::GetServersDirectory() const
	{
		return m_ServersDirectory;
	}

	void ServerInstalls::SweepAtStartup()
	{
		m_Submit([serversDirectory = m_ServersDirectory] { SweepServersDirectory(serversDirectory); });
		m_Submit([serversDirectory = m_ServersDirectory, read = m_InstalledRead] { ReadInstalledVersions(serversDirectory, *read); });
	}

	bool ServerInstalls::Install(std::string_view serverId)
	{
		const ServerDefinition* server = FindServer(serverId);

		return NoteStarted(serverId, InstallJobKind::Install,
						   !IsBusy(serverId) && m_Http != nullptr && server != nullptr && m_Installer.Install(*server));
	}

	bool ServerInstalls::CheckForUpdates(std::string_view serverId)
	{
		const ServerDefinition* server = FindServer(serverId);

		return NoteStarted(serverId, InstallJobKind::CheckForUpdates,
						   !IsBusy(serverId) && m_Http != nullptr && server != nullptr && m_Installer.CheckForUpdates(*server));
	}

	bool ServerInstalls::Uninstall(std::string_view serverId)
	{
		const ServerDefinition* server = FindServer(serverId);

		return NoteStarted(serverId, InstallJobKind::Uninstall, !IsBusy(serverId) && server != nullptr && m_Installer.Uninstall(*server));
	}

	// A job is refused until the last one's outcome has been polled: that outcome would erase the newer job's state.
	bool ServerInstalls::NoteStarted(std::string_view serverId, InstallJobKind kind, bool started)
	{
		if (started)
		{
			m_Running.insert_or_assign(std::string{serverId}, kind);
		}

		return started;
	}

	void ServerInstalls::Cancel(std::string_view serverId)
	{
		if (m_Running.contains(serverId))
		{
			m_Cancelling.insert(std::string{serverId});
		}

		m_Installer.Cancel(serverId);
	}

	bool ServerInstalls::IsBusy(std::string_view serverId) const
	{
		return m_Running.contains(serverId) || m_Installer.IsBusy(serverId);
	}

	bool ServerInstalls::CanDownload() const
	{
		return m_Http != nullptr;
	}

	bool ServerInstalls::HasReadInstalls() const
	{
		return m_HaveReadInstalls;
	}

	std::optional<InstalledVersion> ServerInstalls::InstalledVersionOf(std::string_view serverId) const
	{
		const VersionsByServer::const_iterator installed = m_Installed.find(serverId);

		if (installed == m_Installed.end())
		{
			return std::nullopt;
		}

		return installed->second;
	}

	std::optional<InstallJobKind> ServerInstalls::RunningJobOf(std::string_view serverId) const
	{
		const JobsByServer::const_iterator running = m_Running.find(serverId);

		if (running == m_Running.end())
		{
			return std::nullopt;
		}

		return running->second;
	}

	bool ServerInstalls::IsCancelling(std::string_view serverId) const
	{
		return m_Cancelling.contains(serverId);
	}

	void ServerInstalls::Poll()
	{
		AdoptInstalledRead();

		std::vector<InstallProgress> changes;
		m_Installer.Drain(changes);

		for (InstallProgress& change : changes)
		{
			if (IsFinished(change.Step))
			{
				NoteOutcome(change);
				m_Finished.push_back(change);
			}

			std::string serverId = change.ServerId;
			m_Latest.insert_or_assign(std::move(serverId), std::move(change));
		}
	}

	// Before any outcome is noted: the read ran ahead of every job on the queue, so an outcome is newer.
	void ServerInstalls::AdoptInstalledRead()
	{
		if (m_HaveReadInstalls)
		{
			return;
		}

		const std::scoped_lock lock{m_InstalledRead->Mutex};

		if (!m_InstalledRead->Found)
		{
			return;
		}

		for (const auto& [serverId, version] : *m_InstalledRead->Found)
		{
			m_Installed.insert_or_assign(serverId, version);
		}

		m_HaveReadInstalls = true;
	}

	void ServerInstalls::NoteOutcome(const InstallProgress& outcome)
	{
		m_Running.erase(outcome.ServerId);
		m_Cancelling.erase(outcome.ServerId);

		switch (outcome.Step)
		{
		case InstallStep::Installed:
			m_Installed.insert_or_assign(outcome.ServerId, InstalledVersion{.Version = outcome.Version, .HasChecksum = outcome.HasChecksum});
			break;
		case InstallStep::Uninstalled:
			m_Installed.erase(outcome.ServerId);
			break;
		case InstallStep::Resolving:
		case InstallStep::Downloading:
		case InstallStep::Verifying:
		case InstallStep::Extracting:
		case InstallStep::UpToDate:
		case InstallStep::UpdateAvailable:
		case InstallStep::Cancelled:
		case InstallStep::Failed:
			break;
		}
	}

	std::optional<InstallProgress> ServerInstalls::ProgressOf(std::string_view serverId) const
	{
		const std::map<std::string, InstallProgress, std::less<>>::const_iterator latest = m_Latest.find(serverId);

		if (latest == m_Latest.end())
		{
			return std::nullopt;
		}

		return latest->second;
	}

	void ServerInstalls::TakeFinished(std::vector<InstallProgress>& finished)
	{
		finished.insert(finished.end(), std::make_move_iterator(m_Finished.begin()), std::make_move_iterator(m_Finished.end()));
		m_Finished.clear();
	}

	std::vector<LanguageId> LanguagesServedBy(std::string_view serverId)
	{
		std::vector<LanguageId> languages;

		for (const LanguageId language : LanguagesWithServers())
		{
			if (ListsServer(DefinitionOf(language), serverId))
			{
				languages.push_back(language);
			}
		}

		return languages;
	}
}
