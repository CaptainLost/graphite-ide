#pragma once

#include "Settings/Panel/SettingsDirectory.h"
#include "Settings/Panel/SettingsPage.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct SettingsCardHits
	{
		std::string Heading;
		std::string Note;
		std::vector<SettingsRow> Rows;
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

	// True when every word, already lowered, is found in at least one of the texts whatever its case;
	// true for no words.
	bool EveryWordFound(std::span<const std::string> words, std::span<const std::string_view> texts);

	std::vector<SettingsPageHits> FindSettings(const SettingsDirectory& directory, std::string_view query);

	bool HasHitsUnder(const std::vector<SettingsPageHits>& hits, std::string_view topPageId);
}
