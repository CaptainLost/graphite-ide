#include <doctest/doctest.h>

#include "Sessions/Overview/SessionPlace.h"

using namespace Graphite;

namespace {
	SessionPlace Place(const wchar_t* repository, const char* worktree)
	{
		return {std::filesystem::path{repository}, worktree};
	}

	SessionSummary SummaryWithCwds(const std::map<std::string, UsageByModel>& byCwd)
	{
		SessionSummary summary;
		summary.Cwd = std::filesystem::path{L"D:\\Fallback"};
		summary.Activity.MainUsageByCwd = byCwd;
		return summary;
	}
}

TEST_CASE("A directory inside a worktree belongs to the worktree's repository")
{
	CHECK(PlaceOfDirectory(L"D:\\Repos\\IDE\\.claude\\worktrees\\foamy-whistling-fox\\docs\\adr") ==
		  Place(L"D:\\Repos\\IDE", "foamy-whistling-fox"));
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("The worktree folders are matched ignoring case")
{
	// Windows paths differ only in case without naming another folder.
	CHECK(PlaceOfDirectory(L"D:\\Repos\\IDE\\.CLAUDE\\Worktrees\\x") == Place(L"D:\\Repos\\IDE", "x"));
}
#endif

TEST_CASE("An agent worktree counts as the repository")
{
	CHECK(PlaceOfDirectory(L"D:\\Repos\\IDE\\.claude\\worktrees\\agent-a1b2") == Place(L"D:\\Repos\\IDE", ""));
}

TEST_CASE("A directory outside any worktree is its own place")
{
	CHECK(PlaceOfDirectory(L"D:\\Repos\\IDE") == Place(L"D:\\Repos\\IDE", ""));
	CHECK(PlaceOfDirectory(L"D:\\worktrees\\x") == Place(L"D:\\worktrees\\x", ""));
}

TEST_CASE("A session belongs where most of its main cost was spent")
{
	const UsageByModel cheap = {{{"claude-opus-5", false}, {10, 10, 0, 0, 0}}};
	const UsageByModel dear = {{{"claude-opus-5", false}, {1000000, 100000, 0, 0, 0}}};
	const SessionSummary summary = SummaryWithCwds(
		{{"D:\\Repos\\IDE", cheap}, {"D:\\Repos\\IDE\\.claude\\worktrees\\wt", dear}, {"", dear}});

	CHECK(PlaceOfSession(summary, Pricing::Defaults()) == Place(L"D:\\Repos\\IDE", "wt"));
}

TEST_CASE("Without any priced usage the cwd with most tokens wins")
{
	const UsageByModel small = {{{"claude-unknown-9", false}, {5, 5, 0, 0, 0}}};
	const UsageByModel large = {{{"claude-unknown-9", false}, {500, 500, 0, 0, 0}}};
	const SessionSummary summary = SummaryWithCwds({{"D:\\A", large}, {"D:\\B", small}});

	CHECK(PlaceOfSession(summary, Pricing::Defaults()) == Place(L"D:\\A", ""));
}

TEST_CASE("A session with only a cwd-less usage falls back to its own cwd")
{
	const UsageByModel usage = {{{"claude-opus-5", false}, {5, 5, 0, 0, 0}}};

	CHECK(PlaceOfSession(SummaryWithCwds({{"", usage}}), Pricing::Defaults()) == Place(L"D:\\Fallback", ""));
	CHECK(PlaceOfSession(SummaryWithCwds({}), Pricing::Defaults()) == Place(L"D:\\Fallback", ""));
}
