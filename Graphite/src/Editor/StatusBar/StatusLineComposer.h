#pragma once

#include "Editor/StatusBar/ServerHealth.h"
#include "Editor/StatusBar/ServerStatusText.h"
#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Install/ServerInstaller.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	std::optional<std::string> ProblemCountText(std::size_t errors, std::size_t warnings);

	struct ServerJob
	{
		InstallJobKind Kind = InstallJobKind::Install;
		std::string Server;
		std::optional<int> Percent;
	};

	struct ServerLineInputs
	{
		ServerStatus Active;

		std::optional<ServerJob> ActiveJob;

		std::optional<std::string> AnalysingFile;

		LanguageId ActiveLanguage = LanguageId::PlainText;

		bool FolderOpen = false;
		bool FolderTrusted = false;

		std::optional<ServerStatus> Overall;

		std::string ActiveReport;
	};

	struct ServerLine
	{
		std::string Text;
		std::string Detail;
		std::optional<LanguageId> SettingsLanguage;
		ServerHealth Health = ServerHealth::None;
	};

	std::optional<ServerLine> ChooseServerLine(const ServerLineInputs& inputs);
}
