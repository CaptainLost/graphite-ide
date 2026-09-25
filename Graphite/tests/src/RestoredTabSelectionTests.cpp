#include <doctest/doctest.h>

#include "Persistence/RestoredTabSelection.h"

using namespace Graphite;

TEST_CASE("A restored view is asked for once and reported until the tab bar shows it")
{
	RestoredTabSelection selection;
	selection.Restore(SessionTab::Cost);

	// The frame that asks: the tab bar still draws the conversation as selected.
	CHECK(selection.ShouldSelect(SessionTab::Cost));
	CHECK_FALSE(selection.ShouldSelect(SessionTab::Conversation));
	selection.AfterTabBar(SessionTab::Conversation);

	CHECK(selection.Reported(SessionTab::Conversation) == SessionTab::Cost);

	// The next frame the ask has taken effect, and it is not repeated.
	CHECK_FALSE(selection.ShouldSelect(SessionTab::Cost));
	selection.AfterTabBar(SessionTab::Cost);

	CHECK_FALSE(selection.IsPending());
	CHECK(selection.Reported(SessionTab::Cost) == SessionTab::Cost);

	// From here the reader's own choice is what is reported.
	selection.AfterTabBar(SessionTab::Requests);
	CHECK(selection.Reported(SessionTab::Requests) == SessionTab::Requests);
}

TEST_CASE("A restored view already in front settles on its first frame")
{
	RestoredTabSelection selection;
	selection.Restore(SessionTab::Conversation);

	CHECK(selection.ShouldSelect(SessionTab::Conversation));
	selection.AfterTabBar(SessionTab::Conversation);

	CHECK_FALSE(selection.IsPending());
}

TEST_CASE("A reader who picks another view as the restored one is asked for keeps their pick")
{
	RestoredTabSelection selection;
	selection.Restore(SessionTab::Requests);

	selection.AfterTabBar(SessionTab::Conversation);

	// The tab bar answered with the reader's click rather than the ask: it is not asked again,
	// and what it shows is reported.
	CHECK_FALSE(selection.ShouldSelect(SessionTab::Requests));
	selection.AfterTabBar(SessionTab::Cost);

	CHECK_FALSE(selection.IsPending());
	CHECK(selection.Reported(SessionTab::Cost) == SessionTab::Cost);
}

TEST_CASE("Nothing restored asks for nothing and reports the tab bar's selection")
{
	RestoredTabSelection selection;

	CHECK_FALSE(selection.ShouldSelect(SessionTab::Conversation));
	CHECK(selection.Reported(SessionTab::Requests) == SessionTab::Requests);

	selection.AfterTabBar(SessionTab::Cost);
	CHECK_FALSE(selection.IsPending());
}
