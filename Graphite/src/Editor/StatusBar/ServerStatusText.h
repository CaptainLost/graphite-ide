#pragma once

#include "Editor/StatusBar/StatusBar.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"

#include <cstddef>
#include <string>

namespace Graphite {

	std::string ServerStatusText(const ServerStatus& status);

	std::string ServerStatusDetail(const ServerStatus& status);

	ServerHealth ServerHealthOf(ServerState state);

	std::string AnalysingText(const ServerStatus& status, const std::string& fileName);

	std::string NoServerText(LanguageId language);

	std::string NoServerDetail(LanguageId language);

	std::string BackgroundAnalysisText(const AnalysisProgress& progress);

	std::string BackgroundAnalysisDetail(const AnalysisProgress& progress, std::size_t keptBytes,
										 std::size_t budgetBytes);
}
