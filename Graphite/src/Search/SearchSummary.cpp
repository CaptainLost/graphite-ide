#include "Search/SearchSummary.h"

#include "Core/Base/Strings.h"

#include <format>

namespace Graphite {

	std::optional<std::string> SearchSummaryText(std::size_t hitCount, const SearchProgress& progress,
												 const std::optional<std::string>& searchedNeedle,
												 std::string_view typedNeedle)
	{
		const bool searchedSomething = searchedNeedle.has_value() && !searchedNeedle->empty();

		if (!searchedSomething)
		{
			return std::nullopt;
		}

		std::string summary = Core::Counted(static_cast<long long>(hitCount), "hit");

		const bool queryEditedSinceSearch = typedNeedle != *searchedNeedle;

		if (queryEditedSinceSearch)
		{
			summary += std::format(" for \"{}\"", *searchedNeedle);
		}

		summary += ", " + Core::Counted(static_cast<long long>(progress.FilesScanned), "file") + " scanned";

		if (progress.FilesSkipped != 0)
		{
			summary += std::format(", {} skipped", progress.FilesSkipped);
		}

		if (progress.Unreadable != 0)
		{
			summary += std::format(", {} unreadable", progress.Unreadable);
		}

		return summary;
	}
}
