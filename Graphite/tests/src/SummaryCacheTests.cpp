#include <doctest/doctest.h>

#include "Sessions/SummaryCache.h"

#include "TempDirectory.h"

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace Graphite;

namespace {
	SessionSummary Sample()
	{
		SessionSummary summary;
		summary.Id = "abc";
		// A profile directory with a character outside every ANSI codepage's ASCII half.
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

#ifdef _WIN32
TEST_CASE("A save that cannot replace the cache leaves the previous content intact")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.GetPath() / "sessions-cache.json";

	REQUIRE(SaveSummaryCache(file, {Sample()}));

	SessionSummary other = Sample();
	other.Id = "replacement";

	{
		// A reader that does not share delete: the swap into place cannot replace the file
		// while it is open, which is how a second instance reading the cache looks.
		const HANDLE reader = ::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
											OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		REQUIRE(reader != INVALID_HANDLE_VALUE);

		CHECK_FALSE(SaveSummaryCache(file, {other}));

		::CloseHandle(reader);
	}

	const std::vector<SessionSummary> loaded = LoadSummaryCache(file);

	REQUIRE(loaded.size() == 1);
	CHECK(loaded[0] == Sample());

	// The failed save left no temporary beside the cache.
	std::size_t entries = 0;
	for (const auto& entry : std::filesystem::directory_iterator{temp.GetPath()})
	{
		static_cast<void>(entry);
		++entries;
	}
	CHECK(entries == 1);

	CHECK(SaveSummaryCache(file, {other}));
}
#endif
