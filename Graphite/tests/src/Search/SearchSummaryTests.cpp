#include <doctest/doctest.h>

#include "Search/SearchSummary.h"

using namespace Graphite;

namespace {
	constexpr std::size_t kTwelveHits = 12;
	constexpr std::size_t kNoHits = 0;
}

TEST_CASE("A search's summary counts hits and scanned files")
{
	const SearchProgress progress{.FilesScanned = 340};

	CHECK(SearchSummaryText(kTwelveHits, progress, "foo", "foo") == "12 hits, 340 files scanned");
}

TEST_CASE("A search that found nothing says so with the files it scanned")
{
	const SearchProgress progress{.FilesScanned = 1};

	CHECK(SearchSummaryText(kNoHits, progress, "foo", "foo") == "0 hits, 1 file scanned");
}

TEST_CASE("A search's summary names the query its results belong to once the query box was edited")
{
	const SearchProgress progress{.FilesScanned = 340};

	CHECK(SearchSummaryText(kTwelveHits, progress, "foo", "foobar") == "12 hits for \"foo\", 340 files scanned");
}

TEST_CASE("A search's summary adds skipped and unreadable files only when there are some")
{
	const SearchProgress progress{.FilesScanned = 10, .FilesSkipped = 2, .Unreadable = 1};

	CHECK(SearchSummaryText(kTwelveHits, progress, "foo", "foo") == "12 hits, 10 files scanned, 2 skipped, 1 unreadable");
}
