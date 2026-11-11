#pragma once

#include "Search/FolderSearch.h"

#include "Core/FileSystem/WslSharePath.h"
#include "Core/Remote/AgentProtocol.h"

#include <nlohmann/json.hpp>

#include <string>

namespace Graphite {

	class WslAgents;

	// A folder inside WSL listed and searched by its distro's agent on its search lane (stage 4),
	// so no file crosses the share to be read: hits come back named in the share spelling.
	class WslSearch final : public FolderSearch
	{
	public:
		WslSearch(WslAgents& agents, Core::WslSharePath root);

		Core::Result<std::vector<std::string>> ListFiles(bool respectGitIgnore, const Core::CancellationToken& token) override;
		Core::Result<FolderSearchFound> SearchText(const SearchQuery& query, bool respectGitIgnore, const std::vector<std::filesystem::path>& skip,
												   const Core::CancellationToken& token) override;

	private:
		Core::Result<Core::AgentReply> Ask(std::string operation, nlohmann::json arguments, const Core::CancellationToken& token);

		WslAgents& m_Agents;
		Core::WslSharePath m_Root;
	};
}
