#include <doctest/doctest.h>

#include "Sessions/SessionActivity.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/Transcript/TranscriptReader.h"

#include "TempDirectory.h"
#include "TranscriptFixtures.h"

#include <cstdint>
#include <format>
#include <limits>

using namespace Graphite;

namespace {
	SessionActivity Recorded(const std::string& bytes, TranscriptThread thread = TranscriptThread::Main)
	{
		ActivityRecorder recorder{thread};
		for (const TranscriptRecord& record : ParseTranscript(bytes).Records)
		{
			recorder.Add(record);
		}

		SessionActivity activity;
		recorder.AddTo(activity);
		return activity;
	}

	UtcHour HourOn17September(int hour)
	{
		return UtcHour{std::chrono::sys_days{std::chrono::year{2026} / std::chrono::September / 17}} +
			   std::chrono::hours{hour};
	}

	UsageByModel SumOf(const std::map<UtcHour, HourActivity>& hours)
	{
		UsageByModel sum;
		for ([[maybe_unused]] const auto& [hour, activity] : hours)
		{
			for (const auto& [usageKey, counts] : activity.Usage)
			{
				sum[usageKey] += counts;
			}
		}
		return sum;
	}

	const std::string kSecondCwd = "D:\\Repos\\IDE\\.claude\\worktrees\\fox";
}

TEST_CASE("A request written as three records counts once, in its first record's hour")
{
	const SessionActivity activity = Recorded(
		Fixtures::Assistant("req-1", Fixtures::ThinkingBlock("hm"), Fixtures::Usage(10, 20, 30, 0, 40), "claude-opus-5",
							"2026-09-17T09:59:59.000Z") +
		Fixtures::Assistant("req-1", Fixtures::TextBlock("ok"), Fixtures::Usage(10, 20, 30, 0, 40), "claude-opus-5",
							"2026-09-17T10:00:00.000Z") +
		Fixtures::Assistant("req-1", Fixtures::ToolUseBlock("t", "Bash", {}), Fixtures::Usage(10, 20, 30, 0, 40),
							"claude-opus-5", "2026-09-17T10:00:01.000Z"));

	REQUIRE(activity.MainHours.size() == 1);
	REQUIRE(activity.MainHours.contains(HourOn17September(9)));
	const HourActivity& hour = activity.MainHours.at(HourOn17September(9));
	CHECK(hour.Requests == 1);
	CHECK(hour.Usage == UsageByModel{{{"claude-opus-5", false}, TokenCounts{10, 20, 30, 0, 40}}});
}

TEST_CASE("An hour counts each request under its own model")
{
	const SessionActivity activity = Recorded(
		Fixtures::Assistant("req-1", Fixtures::TextBlock("a"), Fixtures::Usage(10, 20), "claude-opus-5",
							"2026-09-17T09:10:00.000Z") +
		Fixtures::Assistant("req-1", Fixtures::TextBlock("b"), Fixtures::Usage(10, 25), "claude-opus-5",
							"2026-09-17T09:10:01.000Z") +
		Fixtures::Assistant("req-2", Fixtures::TextBlock("c"), Fixtures::Usage(5, 5), "claude-haiku-4-5",
							"2026-09-17T09:20:00.000Z"));

	REQUIRE(activity.MainHours.contains(HourOn17September(9)));
	const HourActivity& hour = activity.MainHours.at(HourOn17September(9));
	CHECK(hour.Requests == 2);
	CHECK(hour.RequestsByModel ==
		  std::map<UsageKey, std::size_t>{{{"claude-opus-5", false}, 1}, {{"claude-haiku-4-5", false}, 1}});
}

TEST_CASE("A request split across an hour boundary lands once, in its first record's hour")
{
	const SessionActivity activity = Recorded(
		Fixtures::Assistant("req-1", Fixtures::ThinkingBlock(""), Fixtures::Usage(2, 2, 304, 0, 67877), "claude-opus-5",
							"2026-09-17T09:59:59.000Z") +
		Fixtures::Assistant("req-1", Fixtures::ToolUseBlock("t", "Bash", {}), Fixtures::Usage(2, 657, 304, 0, 67877),
							"claude-opus-5", "2026-09-17T10:00:02.000Z"));

	REQUIRE(activity.MainHours.contains(HourOn17September(9)));
	CHECK(activity.MainHours.at(HourOn17September(9)).Usage.at({"claude-opus-5", false}) ==
		  TokenCounts{2, 657, 304, 0, 67877});
	CHECK_FALSE(activity.MainHours.contains(HourOn17September(10)));
}

TEST_CASE("Usage per cwd follows each request's own cwd")
{
	const SessionActivity activity = Recorded(
		Fixtures::Assistant("a", Fixtures::TextBlock("x"), Fixtures::Usage(1, 2), "claude-opus-5",
							"2026-09-17T09:00:01.000Z", nullptr, "D:\\Repos\\IDE") +
		Fixtures::Assistant("b", Fixtures::TextBlock("y"), Fixtures::Usage(3, 4), "claude-opus-5",
							"2026-09-17T09:00:02.000Z", nullptr, kSecondCwd));

	CHECK(activity.MainUsageByCwd.size() == 2);
	CHECK(activity.MainUsageByCwd.at("D:\\Repos\\IDE") == UsageByModel{{{"claude-opus-5", false}, {1, 2, 0, 0, 0}}});
	CHECK(activity.MainUsageByCwd.at(kSecondCwd) == UsageByModel{{{"claude-opus-5", false}, {3, 4, 0, 0, 0}}});
}

TEST_CASE("Prompts count typed prompts only")
{
	const SessionActivity activity =
		Recorded(Fixtures::UserPrompt("Real question", "2026-09-17T09:00:00.000Z") +
				 Fixtures::MetaUser("skill body", "2026-09-17T09:00:01.000Z") +
				 Fixtures::UserPrompt("<command-name>/clear</command-name>", "2026-09-17T09:00:02.000Z"));

	REQUIRE(activity.MainHours.contains(HourOn17September(9)));
	CHECK(activity.MainHours.at(HourOn17September(9)).Prompts == 1);
}

TEST_CASE("A compact boundary counts one compaction")
{
	const SessionActivity activity =
		Recorded(Fixtures::UserPrompt("Go") + Fixtures::CompactBoundary("2026-09-17T09:10:00.000Z"));

	CHECK(activity.Compactions == 1);
}

TEST_CASE("Active time adds only gaps shorter than ten minutes")
{
	const SessionActivity activity = Recorded(
		Fixtures::UserPrompt("a", "2026-09-17T09:00:00.000Z") + Fixtures::UserPrompt("c", "2026-09-17T09:30:00.000Z") +
		Fixtures::UserPrompt("b", "2026-09-17T09:05:00.000Z") + Fixtures::UserPrompt("d", "2026-09-17T09:39:00.000Z"));

	CHECK(activity.ActiveTime == std::chrono::minutes{14});
}

TEST_CASE("A record dated more than five years back or past tomorrow counts in no hour")
{
	const std::string lastYear = std::format("{:%Y}-01-01T09:00:00.000Z", UtcToday() - std::chrono::days{366});
	const SessionActivity activity = Recorded(
		Fixtures::Assistant("old", Fixtures::TextBlock("a"), Fixtures::Usage(1, 1, 0, 0, 0), "claude-opus-5",
							"1970-01-01T00:00:00.000Z") +
		Fixtures::Assistant("ancient", Fixtures::TextBlock("b"), Fixtures::Usage(1, 1, 0, 0, 0), "claude-opus-5",
							"0001-01-01T00:00:00.000Z") +
		Fixtures::Assistant("future", Fixtures::TextBlock("c"), Fixtures::Usage(1, 1, 0, 0, 0), "claude-opus-5",
							"9999-12-31T23:00:00.000Z") +
		Fixtures::Assistant("recent", Fixtures::TextBlock("d"), Fixtures::Usage(1, 1, 0, 0, 0), "claude-opus-5", lastYear));

	REQUIRE(activity.MainHours.size() == 1);
	CHECK(activity.MainHours.begin()->second.Requests == 1);
	CHECK(activity.MainUsageByCwd.at("D:\\Repos\\IDE").at({"claude-opus-5", false}).Input == 4);
}

TEST_CASE("The usage range runs from five years before today to the end of tomorrow")
{
	const std::chrono::sys_days today{std::chrono::year{2026} / 11 / 10};

	CHECK(IsWithinUsageRange(UtcHour{today}, today));
	CHECK(IsWithinUsageRange(UtcHour{today + std::chrono::days{2}} - std::chrono::hours{1}, today));
	CHECK_FALSE(IsWithinUsageRange(UtcHour{today + std::chrono::days{2}}, today));
	CHECK(IsWithinUsageRange(UtcHour{OldestUsageDay(today)}, today));
	CHECK_FALSE(IsWithinUsageRange(UtcHour{OldestUsageDay(today)} - std::chrono::hours{1}, today));
	CHECK(OldestUsageDay(today) <= std::chrono::sys_days{std::chrono::year{2021} / 11 / 10});
	CHECK(OldestUsageDay(today) >= std::chrono::sys_days{std::chrono::year{2021} / 11 / 9});
	CHECK_FALSE(IsWithinUsageRange(UtcHour{std::chrono::hours{std::numeric_limits<std::int64_t>::max()}}, today));
	CHECK_FALSE(IsWithinUsageRange(UtcHour{std::chrono::hours{std::numeric_limits<std::int64_t>::min()}}, today));
}

TEST_CASE("A failed tool result is charged to the tool it answers")
{
	const SessionActivity activity =
		Recorded(Fixtures::Assistant("r", Fixtures::ToolUseBlock("t1", "PowerShell", nlohmann::json::object()),
									 Fixtures::Usage(1, 1)) +
				 Fixtures::ToolResult("t1", "boom", true) + Fixtures::ToolResult("unknown", "boom", true));

	CHECK(activity.Tools == std::map<std::string, ToolTally>{{"PowerShell", ToolTally{1, 1}}});
}

TEST_CASE("MCP tools are grouped by server")
{
	const SessionActivity activity = Recorded(Fixtures::Assistant(
		"r", Fixtures::ToolUseBlock("t1", "mcp__blender__get_scene", nlohmann::json::object()), Fixtures::Usage(1, 1)));

	CHECK(activity.Tools == std::map<std::string, ToolTally>{{"mcp:blender", ToolTally{1, 0}}});
	CHECK(ToolGroupName("mcp__claude-in-chrome__navigate") == "mcp:claude-in-chrome");
	CHECK(ToolGroupName("Bash") == "Bash");
}

TEST_CASE("A skill call is named by its input and keeps its latest time")
{
	const nlohmann::json input = {{"skill", "superpowers:brainstorming"}};
	const SessionActivity activity =
		Recorded(Fixtures::Assistant("a", Fixtures::ToolUseBlock("s2", "Skill", input), Fixtures::Usage(1, 1),
									 "claude-opus-5", "2026-09-17T09:30:01.000Z") +
				 Fixtures::Assistant("b", Fixtures::ToolUseBlock("s1", "Skill", input), Fixtures::Usage(1, 1),
									 "claude-opus-5", "2026-09-17T09:00:01.000Z"));

	CHECK(activity.Skills == std::map<std::string, SkillTally>{
								 {"superpowers:brainstorming", SkillTally{2, "2026-09-17T09:30:01.000Z"}}});
	CHECK(activity.Tools.at("Skill").Calls == 2);
}

namespace {
	std::filesystem::path SessionWithSubagent(TempDirectory& temp)
	{
		const std::filesystem::path file = temp.MakeFile(
			"proj/abc.jsonl",
			Fixtures::UserPrompt("Go", "2026-09-17T09:00:00.000Z") +
				Fixtures::Assistant("main-1", Fixtures::ToolUseBlock("m1", "Agent", nlohmann::json::object()),
									Fixtures::Usage(1, 2, 3, 0, 4), "claude-opus-5", "2026-09-17T09:00:01.000Z") +
				Fixtures::Assistant("main-2", Fixtures::TextBlock("done"), Fixtures::Usage(5, 6), "claude-opus-5",
									"2026-09-17T11:00:01.000Z"));
		temp.MakeFile("proj/abc/subagents/agent-x.jsonl",
					  Fixtures::Assistant("sub-1", Fixtures::ToolUseBlock("s1", "Grep", nlohmann::json::object()),
										  Fixtures::Usage(7, 8), "claude-sonnet-5", "2026-09-17T09:00:05.000Z",
										  nullptr, kSecondCwd) +
						  Fixtures::ToolResult("s1", "no", true, "2026-09-17T09:00:06.000Z"));
		return file;
	}
}

TEST_CASE("A subagent's usage goes to SubagentHours and its tools to Tools")
{
	TempDirectory temp;
	const SessionSummary summary = SummariseSession(SessionWithSubagent(temp));

	CHECK_FALSE(summary.Activity.SubagentHours.empty());
	CHECK(summary.Activity.SubagentHours.at(HourOn17September(9)).Requests == 1);
	CHECK_FALSE(summary.Activity.MainUsageByCwd.contains(kSecondCwd));
	CHECK(summary.Activity.MainUsageByCwd.at("D:\\Repos\\IDE") == summary.Main);
	CHECK(summary.Activity.Tools.at("Grep") == ToolTally{1, 1});
	CHECK(summary.Activity.Tools.at("Agent") == ToolTally{1, 0});
}

TEST_CASE("The hourly usage adds up to the session's totals")
{
	TempDirectory temp;
	const SessionSummary summary = SummariseSession(SessionWithSubagent(temp));

	CHECK(SumOf(summary.Activity.MainHours) == summary.Main);
	CHECK(SumOf(summary.Activity.SubagentHours) == summary.Subagents);
}
