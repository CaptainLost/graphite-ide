#include <doctest/doctest.h>

#include "Editor/Find/FindNavigation.h"

#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

namespace {

	Core::OpenItem ItemSearchingFor(std::string needle)
	{
		Core::OpenItem item;

		item.Find.Open = true;
		item.Find.Query = std::move(needle);

		return item;
	}
}

TEST_CASE("Stepping forward from the last match wraps to the first")
{
	const Core::TextBuffer buffer("alpha beta alpha gamma alpha");
	Core::OpenItem item = ItemSearchingFor("alpha");

	Graphite::RefreshFindMatches(item, buffer);

	REQUIRE(item.Find.Matches.size() == 3);

	item.Find.CurrentMatch = 2;

	Graphite::StepFindMatch(item, buffer, Graphite::StepDirection::Forwards);

	CHECK(item.Find.CurrentMatch == 0);

	CHECK(item.Caret.Anchor == 0);
	CHECK(item.Caret.Offset == 5);
	CHECK(item.PendingScroll);
}

TEST_CASE("Stepping back from the first match wraps to the last")
{
	const Core::TextBuffer buffer("alpha beta alpha gamma alpha");
	Core::OpenItem item = ItemSearchingFor("alpha");

	Graphite::RefreshFindMatches(item, buffer);

	REQUIRE(item.Find.Matches.size() == 3);

	item.Find.CurrentMatch = 0;

	Graphite::StepFindMatch(item, buffer, Graphite::StepDirection::Backwards);

	CHECK(item.Find.CurrentMatch == 2);
	CHECK(item.Caret.Anchor == 23);
	CHECK(item.Caret.Offset == 28);
}

TEST_CASE("Stepping through no matches at all does nothing")
{
	const Core::TextBuffer buffer("alpha beta");
	Core::OpenItem item = ItemSearchingFor("gamma");

	Graphite::RefreshFindMatches(item, buffer);

	Graphite::StepFindMatch(item, buffer, Graphite::StepDirection::Forwards);

	CHECK(item.Find.Matches.empty());
	CHECK(item.Caret.Offset == 0);
	CHECK_FALSE(item.PendingScroll);
}

TEST_CASE("A refresh after the buffer shrank leaves the current match inside the new list")
{
	Core::TextBuffer buffer("alpha\nalpha\nalpha\n");
	Core::OpenItem item = ItemSearchingFor("alpha");

	Graphite::RefreshFindMatches(item, buffer);

	REQUIRE(item.Find.Matches.size() == 3);

	item.Find.CurrentMatch = 2;

	buffer.Erase(0, 12);

	Graphite::RefreshFindMatches(item, buffer);

	REQUIRE(item.Find.Matches.size() == 1);
	CHECK(item.Find.CurrentMatch < item.Find.Matches.size());
	CHECK(item.Find.Matches[0].From == 0);
	CHECK(item.Find.Matches[0].To == 5);
	CHECK(item.Find.MatchesRevision == buffer.GetRevision());
}

TEST_CASE("A refresh with an empty query clears the matches")
{
	const Core::TextBuffer buffer("alpha beta alpha");
	Core::OpenItem item = ItemSearchingFor("alpha");

	Graphite::RefreshFindMatches(item, buffer);

	REQUIRE(item.Find.Matches.size() == 2);

	item.Find.Query.clear();

	Graphite::RefreshFindMatches(item, buffer);

	CHECK(item.Find.Matches.empty());
	CHECK(item.Find.CurrentMatch == 0);
}

TEST_CASE("Refreshing twice at the same revision does not rebuild")
{
	Core::TextBuffer buffer("alpha beta alpha");
	Core::OpenItem item = ItemSearchingFor("alpha");

	Graphite::RefreshFindMatches(item, buffer);

	REQUIRE(item.Find.Matches.size() == 2);

	item.Find.Matches.push_back(Core::FindMatch{.From = 99, .To = 100});

	CHECK_FALSE(Graphite::RefreshFindMatchesIfStale(item, buffer));
	CHECK(item.Find.Matches.size() == 3);

	buffer.Insert(0, "alpha ");

	CHECK(Graphite::RefreshFindMatchesIfStale(item, buffer));
	CHECK(item.Find.Matches.size() == 3);
	CHECK(item.Find.Matches[2].From == 17);
}

TEST_CASE("The frame Escape closes the bar on does not write the field back")
{
	CHECK_FALSE(Graphite::AcceptsQueryEdit(Graphite::FindFieldEvents{.Edited = true, .EscapePressed = true}));

	CHECK(Graphite::AcceptsQueryEdit(Graphite::FindFieldEvents{.Edited = true, .EscapePressed = false}));
	CHECK_FALSE(Graphite::AcceptsQueryEdit(Graphite::FindFieldEvents{.Edited = false, .EscapePressed = false}));
	CHECK_FALSE(Graphite::AcceptsQueryEdit(Graphite::FindFieldEvents{.Edited = false, .EscapePressed = true}));
}

TEST_CASE("Revealing the current match selects it and asks for a scroll to it")
{
	const Core::TextBuffer buffer("alpha beta alpha gamma alpha");
	Core::OpenItem item = ItemSearchingFor("alpha");

	item.Caret.Anchor = item.Caret.Offset = 7;

	Graphite::RefreshFindMatches(item, buffer);

	REQUIRE(item.Find.CurrentMatch == 1);

	item.PendingScroll = false;

	Graphite::RevealFindMatch(item, buffer);

	CHECK(item.Find.CurrentMatch == 1);
	CHECK(item.Caret.Anchor == 11);
	CHECK(item.Caret.Offset == 16);
	CHECK(item.PendingScroll);
}

TEST_CASE("A query typed a letter at a time stays on the match it started on")
{
	const Core::TextBuffer buffer("alp alpha alpine");
	Core::OpenItem item = ItemSearchingFor("alpi");

	item.Caret.Anchor = item.Caret.Offset = 5;

	Graphite::RefreshFindMatches(item, buffer);
	Graphite::RevealFindMatch(item, buffer);

	REQUIRE(item.Caret.Anchor == 10);

	item.Find.Query = "alpin";
	Graphite::RefreshFindMatches(item, buffer);
	Graphite::RevealFindMatch(item, buffer);

	CHECK(item.Caret.Anchor == 10);
	CHECK(item.Caret.Offset == 15);
}

TEST_CASE("Revealing with nothing found leaves the caret where it was")
{
	const Core::TextBuffer buffer("alpha beta");
	Core::OpenItem item = ItemSearchingFor("zeta");

	item.Caret.Anchor = item.Caret.Offset = 3;
	item.PendingScroll = false;

	Graphite::RefreshFindMatches(item, buffer);
	Graphite::RevealFindMatch(item, buffer);

	CHECK(item.Caret.Anchor == 3);
	CHECK(item.Caret.Offset == 3);
	CHECK_FALSE(item.PendingScroll);
}
