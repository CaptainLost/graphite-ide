#include "Sessions/SessionTitles.h"

#include <tuple>

namespace Graphite {

	SessionTitles LatestSessionTitles(const std::map<std::filesystem::path, SessionSummary>& sessions)
	{
		std::map<std::filesystem::path::string_type, const SessionSummary*> newest;

		for (const auto& [file, summary] : sessions)
		{
			if (summary.IsEmpty() || summary.Cwd.empty())
			{
				continue;
			}

			const auto [entry, inserted] = newest.try_emplace(DirectoryKey(summary.Cwd), &summary);

			if (!inserted && std::tie(entry->second->EndedAt, entry->second->StartedAt) <
								 std::tie(summary.EndedAt, summary.StartedAt))
			{
				entry->second = &summary;
			}
		}

		SessionTitles titles;

		for (const auto& [key, summary] : newest)
		{
			titles.ByDirectory.emplace(key, summary->DisplayTitle());
		}

		return titles;
	}

	std::optional<std::string> SessionTitleFor(const SessionTitles& titles, const std::filesystem::path& directory)
	{
		const auto found = titles.ByDirectory.find(DirectoryKey(directory));
		return found == titles.ByDirectory.end() ? std::nullopt : std::optional<std::string>{found->second};
	}
}
