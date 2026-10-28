#include <doctest/doctest.h>

#include "Git/History/CommitReveal.h"
#include "Git/History/HistoryReach.h"

#include "GitFixtures.h"

#include "Core/Git/Commit.h"
#include "Core/Git/CommitFilter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>

using Graphite::CommitReveal;
using Graphite::HistoryPathText;
using Graphite::HistoryReach;
using Graphite::NextRevealStep;
using Graphite::RevealStep;
using Graphite::ShowsWholeHeadHistory;

namespace {

	constexpr bool kComplete = true;
	constexpr bool kStoppedShort = false;

	// The page a walk with the first reach leaves when it stops at its 500 commits.
	constexpr std::size_t kWalkedToTheFirstReach = 500;

	Core::LogPage PageOf(std::initializer_list<std::uint8_t> firstBytes, bool complete, std::size_t walked)
	{
		Core::LogPage page;
		page.Complete = complete;
		page.Walked = walked;

		for (const std::uint8_t firstByte : firstBytes)
		{
			Core::CommitSummary commit;
			commit.Id = OidWithFirstByte(firstByte);
			page.Commits.push_back(commit);
		}

		return page;
	}

	CommitReveal RevealOf(std::uint8_t firstByte)
	{
		return CommitReveal{.Commit = OidWithFirstByte(firstByte), .Path = "Assets/Game/EventBus.cs"};
	}
}

TEST_CASE("Commit reveal selects a commit on the page")
{
	const HistoryReach asked;

	CHECK(NextRevealStep(RevealOf(2), PageOf({1, 2, 3}, kStoppedShort, kWalkedToTheFirstReach), asked) ==
		  RevealStep::Select);
	CHECK(NextRevealStep(RevealOf(2), PageOf({1, 2, 3}, kComplete, 3), asked) == RevealStep::Select);
}

TEST_CASE("Commit reveal loads more when the page is not complete")
{
	CHECK(NextRevealStep(RevealOf(9), PageOf({1, 2, 3}, kStoppedShort, kWalkedToTheFirstReach), HistoryReach{}) ==
		  RevealStep::LoadMore);
}

TEST_CASE("Commit reveal falls back to the hash when the history is complete")
{
	const HistoryReach asked;

	CHECK(NextRevealStep(RevealOf(9), PageOf({1, 2, 3}, kComplete, 3), asked) == RevealStep::FilterByHash);
	CHECK(NextRevealStep(RevealOf(9), PageOf({}, kComplete, 0), asked) == RevealStep::FilterByHash);
}

TEST_CASE("Commit reveal falls back to the hash once the walk bound is reached")
{
	const HistoryReach asked{.MaxCount = Core::kLogWalkLimit, .MaxWalk = Core::kLogWalkLimit};

	CHECK(NextRevealStep(RevealOf(9), PageOf({1, 2, 3}, kStoppedShort, Core::kLogWalkLimit), asked) ==
		  RevealStep::FilterByHash);
}

TEST_CASE("Commit reveal falls back to the hash when the walk ended short of its reach")
{
	// An unreadable commit leaves the page incomplete even where the history ran out, so asking again
	// for more would only read the same page forever.
	const HistoryReach asked{.MaxCount = 1000, .MaxWalk = Core::kLogWalkLimit};

	CHECK(NextRevealStep(RevealOf(9), PageOf({1, 2, 3}, kStoppedShort, 600), asked) == RevealStep::FilterByHash);
}

TEST_CASE("Commit reveal carries on only over HEAD's whole history")
{
	CHECK(ShowsWholeHeadHistory(Core::LogRequest{}));

	Core::LogRequest onlySpaces;
	onlySpaces.Filter.Text = "   ";
	CHECK(ShowsWholeHeadHistory(onlySpaces));

	Core::LogRequest branch;
	branch.Ref = "refs/heads/feature";
	CHECK_FALSE(ShowsWholeHeadHistory(branch));

	Core::LogRequest stash;
	stash.Stash = OidWithFirstByte(7);
	CHECK_FALSE(ShowsWholeHeadHistory(stash));

	Core::LogRequest typed;
	typed.Filter.Text = "fix";
	CHECK_FALSE(ShowsWholeHeadHistory(typed));

	Core::LogRequest file;
	file.File = std::filesystem::path("Assets/Game/EventBus.cs");
	CHECK_FALSE(ShowsWholeHeadHistory(file));
}

TEST_CASE("Commit reveal writes the history path with forward slashes")
{
	CHECK(HistoryPathText(std::filesystem::path(L"Assets\\Game\\EventBus.cs")) == "Assets/Game/EventBus.cs");
}

TEST_CASE("Commit reveal writes a root-level history path that names only that file")
{
	const std::string pathText = HistoryPathText(std::filesystem::path(L"README.md"));

	CHECK(pathText == "/README.md");
	CHECK(Core::NamesPath(pathText, "README.md"));
	CHECK_FALSE(Core::NamesPath(pathText, "docs/README.md"));
	CHECK_FALSE(Core::NamesPath(pathText, "src/README.md.bak"));
}
