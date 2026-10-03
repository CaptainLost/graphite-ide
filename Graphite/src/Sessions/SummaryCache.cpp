#include "Sessions/SummaryCache.h"

#include "Core/Fs/AtomicWrite.h"
#include "Core/Fs/FileSystem.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace Graphite {

	namespace {
		nlohmann::json UsageJson(const UsageByModel& usage)
		{
			nlohmann::json rows = nlohmann::json::array();

			for (const auto& [key, counts] : usage)
			{
				rows.push_back({{"model", key.Model},
								{"fast", key.Fast},
								{"counts",
								 {counts.Input, counts.Output, counts.CacheWrite5m, counts.CacheWrite1h,
								  counts.CacheRead}}});
			}

			return rows;
		}

		UsageByModel UsageFrom(const nlohmann::json& rows)
		{
			UsageByModel usage;

			for (const nlohmann::json& row : rows)
			{
				const auto counts = row.at("counts").get<std::vector<std::uint64_t>>();

				if (counts.size() != 5)
				{
					throw nlohmann::json::other_error::create(501, "counts must have five entries", &row);
				}

				usage[{row.at("model").get<std::string>(), row.at("fast").get<bool>()}] =
					TokenCounts{counts[0], counts[1], counts[2], counts[3], counts[4]};
			}

			return usage;
		}
	}

	std::vector<SessionSummary> LoadSummaryCache(const std::filesystem::path& file)
	{
		std::ifstream stream{file, std::ios::binary};

		if (!stream)
		{
			return {};
		}

		std::stringstream buffer;
		buffer << stream.rdbuf();

		const nlohmann::json root = nlohmann::json::parse(buffer.str(), nullptr, false);

		// Not value("version", 0): it throws when the field is there with another type.
		if (root.is_discarded() || !root.is_object())
		{
			return {};
		}

		const auto version = root.find("version");

		if (version == root.end() || !version->is_number_integer() || *version != kSummaryCacheVersion)
		{
			return {};
		}

		std::vector<SessionSummary> summaries;

		// `at` and `get` throw on a missing or mistyped field. One malformed entry means the
		// file is not one this build wrote, so the whole cache goes, not only that entry.
		try
		{
			for (const nlohmann::json& entry : root.at("sessions"))
			{
				SessionSummary summary;
				summary.Id = entry.at("id").get<std::string>();
				summary.File = Core::FromUtf8(entry.at("file").get<std::string>());
				summary.Cwd = Core::FromUtf8(entry.at("cwd").get<std::string>());
				summary.Title = entry.at("title").get<std::string>();
				summary.FirstPrompt = entry.at("firstPrompt").get<std::string>();
				summary.StartedAt = entry.at("startedAt").get<std::string>();
				summary.EndedAt = entry.at("endedAt").get<std::string>();
				summary.Main = UsageFrom(entry.at("main"));
				summary.Subagents = UsageFrom(entry.at("subagents"));
				summary.SubagentCount = entry.at("subagentCount").get<std::size_t>();
				summary.SkippedLines = entry.at("skippedLines").get<std::size_t>();
				summary.Print.WriteTicks = entry.at("writeTicks").get<std::int64_t>();
				summary.Print.Bytes = entry.at("bytes").get<std::uint64_t>();
				summary.Print.Files = entry.at("files").get<std::size_t>();
				summaries.push_back(std::move(summary));
			}
		}
		catch (const nlohmann::json::exception&)
		{
			return {};
		}

		return summaries;
	}

	Core::Result<void> SaveSummaryCache(const std::filesystem::path& file, const std::vector<SessionSummary>& summaries)
	{
		nlohmann::json sessions = nlohmann::json::array();

		for (const SessionSummary& summary : summaries)
		{
			sessions.push_back({{"id", summary.Id},
								{"file", Core::ToUtf8(summary.File)},
								{"cwd", Core::ToUtf8(summary.Cwd)},
								{"title", summary.Title},
								{"firstPrompt", summary.FirstPrompt},
								{"startedAt", summary.StartedAt},
								{"endedAt", summary.EndedAt},
								{"main", UsageJson(summary.Main)},
								{"subagents", UsageJson(summary.Subagents)},
								{"subagentCount", summary.SubagentCount},
								{"skippedLines", summary.SkippedLines},
								{"writeTicks", summary.Print.WriteTicks},
								{"bytes", summary.Print.Bytes},
								{"files", summary.Print.Files}});
		}

		const nlohmann::json root = {{"version", kSummaryCacheVersion}, {"sessions", std::move(sessions)}};

		// Swapped into place, so a crash mid-write or a second instance saving at the same
		// moment leaves the old cache or the new one, never a truncated file.
		return Core::WriteFileAtomically(file, root.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
	}
}
