#pragma once

#include "Sessions/SessionSummary.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace Graphite {

	struct SessionTitles
	{
		std::map<std::filesystem::path::string_type, std::string> ByDirectory;
	};

	// The newest non-empty session per working directory names it: newest by EndedAt, then StartedAt (ISO text
	// compares in time order).
	SessionTitles LatestSessionTitles(const std::map<std::filesystem::path, SessionSummary>& sessions);

	std::optional<std::string> SessionTitleFor(const SessionTitles& titles, const std::filesystem::path& directory);
}
