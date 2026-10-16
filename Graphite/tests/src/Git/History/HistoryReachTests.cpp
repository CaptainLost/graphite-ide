#include <doctest/doctest.h>

#include "Git/History/HistoryReach.h"

using Graphite::Everything;
using Graphite::HistoryReach;
using Graphite::kNoHistoryLimit;
using Graphite::LoadMore;
using Graphite::ReachAfterClick;
using Graphite::SearchFurther;

TEST_CASE("The first page is the newest 500 commits within a walk of 20000")
{
	const HistoryReach reach;

	CHECK(reach.MaxCount == 500);
	CHECK(reach.MaxWalk == 20000);
}

TEST_CASE("Load more asks for 500 more commits within the same ceiling while the page is under it")
{
	CHECK(LoadMore(HistoryReach{}) == HistoryReach{.MaxCount = 1000, .MaxWalk = 20000});
}

TEST_CASE("Load more lifts the ceiling with the page once the page outgrows it so a long history reads past its newest 20000")
{
	CHECK(LoadMore(HistoryReach{.MaxCount = 20000, .MaxWalk = 20000}) == HistoryReach{.MaxCount = 20500, .MaxWalk = 20500});
}

TEST_CASE("Search further asks for 500 more matches or 20000 more commits walked than the page on screen got")
{
	CHECK(SearchFurther(37, 20000) == HistoryReach{.MaxCount = 537, .MaxWalk = 40000});
}

TEST_CASE("Everything lifts both limits")
{
	CHECK(Everything() == HistoryReach{.MaxCount = kNoHistoryLimit, .MaxWalk = kNoHistoryLimit});
}

TEST_CASE("Asking for more after everything stays at no limit instead of wrapping around")
{
	CHECK(LoadMore(Everything()) == Everything());
	CHECK(SearchFurther(kNoHistoryLimit, kNoHistoryLimit) == Everything());
}

TEST_CASE("Load all and Search all ask for everything whatever the page on screen reached")
{
	CHECK(ReachAfterClick(true, false, HistoryReach{}, 500, 500) == Everything());
	CHECK(ReachAfterClick(true, true, HistoryReach{}, 37, 20000) == Everything());
}

TEST_CASE("Search further steps on from the matches and the walk of the page on screen, not from its reach")
{
	const HistoryReach shown{.MaxCount = 500, .MaxWalk = 20000};

	CHECK(ReachAfterClick(false, true, shown, 37, 20000) == SearchFurther(37, 20000));
}

TEST_CASE("Load 500 more steps on from the reach of the page on screen")
{
	const HistoryReach shown{.MaxCount = 1000, .MaxWalk = 20000};

	CHECK(ReachAfterClick(false, false, shown, 1000, 1000) == LoadMore(shown));
}
