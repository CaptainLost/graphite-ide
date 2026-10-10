#include "Language/Install/ServerInstalls.h"

#include "Language/Install/InstalledServer.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Server/ServerCatalog.h"

#include "Core/Platform/CurrentPlatform.h"

#include <iterator>
#include <utility>

namespace Graphite {

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
	}

	ServerInstalls::ServerInstalls(Core::Ref<Core::HttpClient> http, std::filesystem::path serversDirectory, Core::JobSubmitter submit)
		: m_Http(std::move(http)),
		  m_ServersDirectory(std::move(serversDirectory)),
		  m_Submit(std::move(submit)),
		  m_Installer(m_Http, m_ServersDirectory, Core::CurrentPlatform(), m_Submit)
	{
	}

	const std::filesystem::path& ServerInstalls::GetServersDirectory() const
	{
		return m_ServersDirectory;
	}

	void ServerInstalls::SweepAtStartup()
	{
		m_Submit([serversDirectory = m_ServersDirectory] { SweepServersDirectory(serversDirectory); });
	}

	bool ServerInstalls::Install(std::string_view serverId)
	{
		const ServerDefinition* server = FindServer(serverId);

		return m_Http != nullptr && server != nullptr && m_Installer.Install(*server);
	}

	bool ServerInstalls::CheckForUpdates(std::string_view serverId)
	{
		const ServerDefinition* server = FindServer(serverId);

		return m_Http != nullptr && server != nullptr && m_Installer.CheckForUpdates(*server);
	}

	bool ServerInstalls::Uninstall(std::string_view serverId)
	{
		const ServerDefinition* server = FindServer(serverId);

		return server != nullptr && m_Installer.Uninstall(*server);
	}

	void ServerInstalls::Cancel(std::string_view serverId)
	{
		m_Installer.Cancel(serverId);
	}

	bool ServerInstalls::IsBusy(std::string_view serverId) const
	{
		return m_Installer.IsBusy(serverId);
	}

	void ServerInstalls::Poll()
	{
		std::vector<InstallProgress> changes;
		m_Installer.Drain(changes);

		for (InstallProgress& change : changes)
		{
			if (IsFinished(change.Step))
			{
				m_Finished.push_back(change);
			}

			std::string serverId = change.ServerId;
			m_Latest.insert_or_assign(std::move(serverId), std::move(change));
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
