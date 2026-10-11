#pragma once

#include "Settings/Panel/SettingsDirectory.h"
#include "Settings/Panel/SettingsPage.h"

#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct SettingsRowHit
	{
		SettingsRow Row;

		// What a block found inside itself ("3 commands match"); empty for a plain row, and for a
		// block found only by its title or keywords.
		std::string MatchesInside;
	};

	struct SettingsCardHits
	{
		std::string Heading;
		std::string Note;
		std::vector<SettingsRowHit> Rows;
	};

	struct SettingsPageHits
	{
		std::vector<std::string> Path;
		std::string Label;
		std::vector<SettingsCardHits> Cards;
	};

	// The query's words, lowered, with empty ones dropped.
	std::vector<std::string> SplitQueryWords(std::string_view query);

	bool IsSearchQuery(std::string_view query);

	std::vector<SettingsPageHits> FindSettings(const SettingsDirectory& directory, std::string_view query);

	bool HasHitsUnder(const std::vector<SettingsPageHits>& hits, std::string_view topPageId);
}
