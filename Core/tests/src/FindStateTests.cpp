#include <doctest/doctest.h>

#include "Core/Text/DocumentSet.h"

TEST_CASE("A newly opened document starts with no find in progress")
{
	const Core::OpenItem item;

	CHECK_FALSE(item.Find.Open);
	CHECK(item.Find.Query.empty());
	CHECK(item.Find.Matches.empty());
	CHECK(item.Find.Current == 0);
	CHECK(item.Find.Revision == 0);
}
