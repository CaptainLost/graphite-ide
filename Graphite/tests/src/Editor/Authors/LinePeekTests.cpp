#include <doctest/doctest.h>

#include "Editor/Authors/LinePeek.h"

#include <cstddef>

using Graphite::PeekClosedBy;
using Graphite::PeekFrame;

namespace {

	constexpr std::size_t kPeekLine = 4;
	constexpr std::size_t kOtherLine = 5;
}

TEST_CASE("Peek closes when the caret leaves its line")
{
	CHECK(PeekClosedBy(PeekFrame{.PeekLine = kPeekLine, .CaretLine = kOtherLine}));
}

TEST_CASE("Peek closes on an edit")
{
	CHECK(PeekClosedBy(PeekFrame{.PeekLine = kPeekLine, .CaretLine = kPeekLine, .Edited = true}));
}

TEST_CASE("Peek closes on request")
{
	CHECK(PeekClosedBy(PeekFrame{.PeekLine = kPeekLine, .CaretLine = kPeekLine, .CloseRequested = true}));
}

TEST_CASE("Peek stays open otherwise")
{
	CHECK_FALSE(PeekClosedBy(PeekFrame{.PeekLine = kPeekLine, .CaretLine = kPeekLine}));
}
