#include <doctest/doctest.h>

#include "Sessions/SummaryCache.h"

#include "TempDirectory.h"

#include <cstdint>
#include <limits>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace Graphite;

namespace {
	SessionSummary Sample()
	{
		SessionSummary summary;
		summary.Id = "abc";
		summary.File = std::filesystem::path{L"C:\\Users\\Micha\u0142\\.claude\\projects\\p\\abc.jsonl"};
		summary.Cwd = std::filesystem::path{L"D:\\Repos\\Zadanie \u017c\u00f3\u0142w"};
		summary.Title = "Tytu\xc5\x82";
		summary.FirstPrompt = "Fix it";
		summary.StartedAt = "2026-09-17T09:00:00.000Z";
		summary.EndedAt = "2026-09-17T10:00:00.000Z";
		summary.Main = {{{"claude-opus-5", false}, {1, 2, 3, 4, 5}}, {{"claude-opus-5", true}, {6, 7, 8, 9, 10}}};
		summary.Subagents = {{{"claude-sonnet-5", false}, {11, 12, 13, 14, 15}}};
		summary.SubagentCount = 3;
		summary.SkippedLines = 1;
		summary.Print = {123456789012345, 4096, 4};

		const UtcHour first{std::chrono::hours{490000}};
		const UsageByModel usage = {{{"claude-opus-5", false}, {1, 2, 3, 4, 5}}};
		summary.Activity.MainHours[first] = {usage, 3, 2, {{{"claude-opus-5", false}, 2}, {{"claude-opus-5", true}, 1}}};
		summary.Activity.MainHours[first + std::chrono::hours{1}] = {usage, 1, 0, {{{"claude-opus-5", false}, 1}}};
		summary.Activity.SubagentHours[first] = {usage, 2, 0, {{{"claude-opus-5", false}, 2}}};
		summary.Activity.MainUsageByCwd["D:\\Projekty\\Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87"] = usage;
		summary.Activity.MainUsageByCwd[""] = usage;
		summary.Activity.Tools["Bash"] = {7, 1};
		summary.Activity.Skills["brainstorming"] = {2, "2026-09-17T09:30:00.000Z"};
		summary.Activity.Compactions = 2;
		summary.Activity.ActiveTime = std::chrono::minutes{75};
		return summary;
	}
}

TEST_CASE("Summaries read back exactly as they were written, non-ASCII paths included")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "sessions-cache.json";

	REQUIRE(SaveSummaryCache(file, {Sample()}));

	const std::vector<SessionSummary> loaded = LoadSummaryCache(file);

	REQUIRE(loaded.size() == 1);
	CHECK(loaded[0] == Sample());
}

TEST_CASE("A missing, corrupt or other-version cache reads as empty")
{
	TempDirectory temp;

	CHECK(LoadSummaryCache(temp.GetPath() / "none.json").empty());
	CHECK(LoadSummaryCache(temp.MakeFile("corrupt.json", "{\"version\":1,\"sessions\":[")).empty());
	CHECK(LoadSummaryCache(temp.MakeFile("old.json", "{\"version\":999,\"sessions\":[]}")).empty());
}

TEST_CASE("A cache whose version is not a number reads as empty rather than throwing")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("odd.json", "{\"version\":\"1\",\"sessions\":[]}");

	std::vector<SessionSummary> loaded;
	REQUIRE_NOTHROW(loaded = LoadSummaryCache(file));
	CHECK(loaded.empty());
}

TEST_CASE("A version 2 cache reads as empty")
{
	TempDirectory temp;

	CHECK(LoadSummaryCache(temp.MakeFile("v2.json", "{\"version\":2,\"sessions\":[]}")).empty());
}

TEST_CASE("A cached hour more than five years back or past tomorrow is dropped, however large its number")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "sessions-cache.json";
	SessionSummary summary = Sample();
	const HourActivity kept = summary.Activity.MainHours.begin()->second;
	summary.Activity.MainHours.clear();
	summary.Activity.MainHours[UtcHour{std::chrono::hours{std::numeric_limits<std::int64_t>::max()}}] = kept;
	summary.Activity.MainHours[UtcHour{std::chrono::hours{std::numeric_limits<std::int64_t>::min()}}] = kept;
	summary.Activity.MainHours[UtcHour{std::chrono::hours{0}}] = kept;
	const UtcHour yesterday{UtcToday() - std::chrono::days{1}};
	summary.Activity.MainHours[yesterday] = kept;

	REQUIRE(SaveSummaryCache(file, {summary}));
	const std::vector<SessionSummary> loaded = LoadSummaryCache(file);

	REQUIRE(loaded.size() == 1);
	REQUIRE(loaded[0].Activity.MainHours.size() == 1);
	CHECK(loaded[0].Activity.MainHours.begin()->first == yesterday);
}

#ifdef _WIN32
TEST_CASE("A save that cannot replace the cache leaves the previous content intact")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "sessions-cache.json";

	REQUIRE(SaveSummaryCache(file, {Sample()}));

	SessionSummary other = Sample();
	other.Id = "replacement";

	{
		const HANDLE readerWithoutShareDelete = ::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
															  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		REQUIRE(readerWithoutShareDelete != INVALID_HANDLE_VALUE);

		CHECK_FALSE(SaveSummaryCache(file, {other}));

		::CloseHandle(readerWithoutShareDelete);
	}

	const std::vector<SessionSummary> loaded = LoadSummaryCache(file);

	REQUIRE(loaded.size() == 1);
	CHECK(loaded[0] == Sample());

	std::size_t filesBesideCache = 0;
	for (const std::filesystem::directory_entry& directoryEntry : std::filesystem::directory_iterator{temp.GetPath()})
	{
		static_cast<void>(directoryEntry);
		++filesBesideCache;
	}
	CHECK(filesBesideCache == 1);

	CHECK(SaveSummaryCache(file, {other}));
}
#endif

