#include <doctest/doctest.h>

#include "Search/PreviewWindow.h"

using namespace Graphite;

TEST_CASE("A hit in the middle of a long file is centred in the window")
{
	CHECK(WindowAround(100, 1000, 21) == PreviewWindow{90, 110});
}

TEST_CASE("A hit near the top starts the window at the first line")
{
	CHECK(WindowAround(2, 1000, 21) == PreviewWindow{0, 20});
	CHECK(WindowAround(0, 1000, 21) == PreviewWindow{0, 20});
	CHECK(WindowAround(10, 1000, 21) == PreviewWindow{0, 20});
	CHECK(WindowAround(11, 1000, 21) == PreviewWindow{1, 21});
}

TEST_CASE("A hit near the end stops the window at the last line")
{
	CHECK(WindowAround(999, 1000, 21) == PreviewWindow{979, 999});
	CHECK(WindowAround(995, 1000, 21) == PreviewWindow{979, 999});
	CHECK(WindowAround(989, 1000, 21) == PreviewWindow{979, 999});
	CHECK(WindowAround(988, 1000, 21) == PreviewWindow{978, 998});
}

TEST_CASE("A file shorter than the window shows all of it")
{
	CHECK(WindowAround(3, 5, 21) == PreviewWindow{0, 4});
	CHECK(WindowAround(0, 1, 21) == PreviewWindow{0, 0});
	CHECK(WindowAround(20, 21, 21) == PreviewWindow{0, 20});
}

TEST_CASE("A line past the end of the file is clamped to the last line")
{
	CHECK(WindowAround(5000, 1000, 21) == PreviewWindow{979, 999});
	CHECK(WindowAround(5000, 5, 21) == PreviewWindow{0, 4});
}

TEST_CASE("An empty buffer answers a single empty line")
{
	CHECK(WindowAround(0, 0, 21) == PreviewWindow{0, 0});
}
