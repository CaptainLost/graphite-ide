#include "Sessions/SummaryCache.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include "Persistence/StoredVersion.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Graphite {

	namespace {
		constexpr std::size_t kTokenCountsPerModel = 5;
		constexpr std::nullptr_t kNoCallback = nullptr;
		constexpr bool kAllowExceptions = false;
		constexpr int kCompactIndent = -1;
		constexpr char kIndentCharacter = ' ';
		constexpr bool kEscapeNonAscii = false;

		nlohmann::json UsageJson(const UsageByModel& usage)
		{
			nlohmann::json rows = nlohmann::json::array();

			for (const auto& [modelKey, counts] : usage)
			{
				rows.push_back({{"model", modelKey.Model},
								{"fast", modelKey.Fast},
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
				const std::vector<std::uint64_t> counts = row.at("counts").get<std::vector<std::uint64_t>>();

				if (counts.size() != kTokenCountsPerModel)
				{
					throw nlohmann::json::other_error::create(501, "counts must have five entries", &row);
				}

				usage[{row.at("model").get<std::string>(), row.at("fast").get<bool>()}] =
					TokenCounts{counts[0], counts[1], counts[2], counts[3], counts[4]};
			}

			return usage;
		}

		nlohmann::json HoursJson(const std::map<UtcHour, HourActivity>& hours)
		{
			nlohmann::json rows = nlohmann::json::array();

			for (const auto& [hour, activity] : hours)
			{
				rows.push_back({hour.time_since_epoch().count(), activity.Requests, activity.Prompts,
								UsageJson(activity.Usage)});
			}

			return rows;
		}

		std::map<UtcHour, HourActivity> HoursFrom(const nlohmann::json& rows)
		{
			std::map<UtcHour, HourActivity> hours;

			for (const nlohmann::json& row : rows)
			{
				HourActivity activity;
				activity.Requests = row.at(1).get<std::size_t>();
				activity.Prompts = row.at(2).get<std::size_t>();
				activity.Usage = UsageFrom(row.at(3));
				hours[UtcHour{std::chrono::hours{row.at(0).get<std::int64_t>()}}] = std::move(activity);
			}

			return hours;
		}

		nlohmann::json ActivityJson(const SessionActivity& activity)
		{
			nlohmann::json cwds = nlohmann::json::array();
			for (const auto& [cwd, usage] : activity.MainUsageByCwd)
			{
				cwds.push_back(nlohmann::json::array({cwd, UsageJson(usage)}));
			}

			nlohmann::json tools = nlohmann::json::array();
			for (const auto& [name, tally] : activity.Tools)
			{
				tools.push_back({name, tally.Calls, tally.Failed});
			}

			nlohmann::json skills = nlohmann::json::array();
			for (const auto& [name, tally] : activity.Skills)
			{
				skills.push_back({name, tally.Calls, tally.LastCalledAt});
			}

			return {{"mainHours", HoursJson(activity.MainHours)},
					{"subagentHours", HoursJson(activity.SubagentHours)},
					{"cwds", std::move(cwds)},
					{"tools", std::move(tools)},
					{"skills", std::move(skills)},
					{"compactions", activity.Compactions},
					{"activeMinutes", activity.ActiveTime.count()}};
		}

		SessionActivity ActivityFrom(const nlohmann::json& json)
		{
			SessionActivity activity;
			activity.MainHours = HoursFrom(json.at("mainHours"));
			activity.SubagentHours = HoursFrom(json.at("subagentHours"));

			for (const nlohmann::json& row : json.at("cwds"))
			{
				activity.MainUsageByCwd[row.at(0).get<std::string>()] = UsageFrom(row.at(1));
			}

			for (const nlohmann::json& row : json.at("tools"))
			{
				activity.Tools[row.at(0).get<std::string>()] =
					ToolTally{row.at(1).get<std::size_t>(), row.at(2).get<std::size_t>()};
			}

			for (const nlohmann::json& row : json.at("skills"))
			{
				activity.Skills[row.at(0).get<std::string>()] =
					SkillTally{row.at(1).get<std::size_t>(), row.at(2).get<std::string>()};
			}

			activity.Compactions = json.at("compactions").get<std::size_t>();
			activity.ActiveTime = std::chrono::minutes{json.at("activeMinutes").get<std::int64_t>()};
			return activity;
		}
	}

	std::vector<SessionSummary> LoadSummaryCache(const std::filesystem::path& file)
	{
		const Core::Result<std::string> bytes = Core::ReadFileBytes(file);

		if (!bytes)
		{
			return {};
		}

		const nlohmann::json root = nlohmann::json::parse(bytes.GetValue(), kNoCallback, kAllowExceptions);

		// WARNING: Reading the version with value("version", 0) would throw when the field has another type.
		if (root.is_discarded() || !root.is_object())
		{
			return {};
		}

		if (StoredVersionOf(root, "version", kSummaryCacheVersion) != StoredVersion::Current)
		{
			return {};
		}

		std::vector<SessionSummary> summaries;

		// WARNING: at and get throw on a missing or mistyped field. One malformed entry means this
		// build did not write the file, so the whole cache goes, not only that entry.
		try
		{
			for (const nlohmann::json& sessionJson : root.at("sessions"))
			{
				SessionSummary summary;
				summary.Id = sessionJson.at("id").get<std::string>();
				summary.File = Core::FromUtf8(sessionJson.at("file").get<std::string>());
				summary.Cwd = Core::FromUtf8(sessionJson.at("cwd").get<std::string>());
				summary.Title = sessionJson.at("title").get<std::string>();
				summary.FirstPrompt = sessionJson.at("firstPrompt").get<std::string>();
				summary.StartedAt = sessionJson.at("startedAt").get<std::string>();
				summary.EndedAt = sessionJson.at("endedAt").get<std::string>();
				summary.Main = UsageFrom(sessionJson.at("main"));
				summary.Subagents = UsageFrom(sessionJson.at("subagents"));
				summary.SubagentCount = sessionJson.at("subagentCount").get<std::size_t>();
				summary.SkippedLines = sessionJson.at("skippedLines").get<std::size_t>();
				summary.Print.WriteTicks = sessionJson.at("writeTicks").get<std::int64_t>();
				summary.Print.Bytes = sessionJson.at("bytes").get<std::uint64_t>();
				summary.Print.Files = sessionJson.at("files").get<std::size_t>();
				summary.Activity = ActivityFrom(sessionJson.at("activity"));
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
								{"files", summary.Print.Files},
								{"activity", ActivityJson(summary.Activity)}});
		}

		const nlohmann::json root = {{"version", kSummaryCacheVersion}, {"sessions", std::move(sessions)}};

		const std::string content =
			root.dump(kCompactIndent, kIndentCharacter, kEscapeNonAscii, nlohmann::json::error_handler_t::replace);
		return Core::WriteFileAtomically(file, content);
	}
}
