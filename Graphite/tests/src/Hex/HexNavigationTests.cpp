#include <doctest/doctest.h>

#include "Hex/HexNavigation.h"

using namespace Graphite;

namespace {

	HexSelection At(std::uint64_t anchor, std::uint64_t active)
	{
		return {anchor, active};
	}
}

TEST_CASE("Arrows move one byte or one row and stop at the file's ends")
{
	constexpr std::uint64_t size = 100;
	CHECK(Moved(At(5, 5), HexMove::Right, false, 10, size).Active == 6);
	CHECK(Moved(At(5, 5), HexMove::Left, false, 10, size).Active == 4);
	CHECK(Moved(At(40, 40), HexMove::Down, false, 10, size).Active == 56);
	CHECK(Moved(At(40, 40), HexMove::Up, false, 10, size).Active == 24);
	CHECK(Moved(At(0, 0), HexMove::Left, false, 10, size).Active == 0);
	CHECK(Moved(At(99, 99), HexMove::Right, false, 10, size).Active == 99);
	CHECK(Moved(At(99, 99), HexMove::Down, false, 10, size).Active == 99);
	CHECK(Moved(At(3, 3), HexMove::Up, false, 10, size).Active == 3);
	CHECK(Moved(At(90, 90), HexMove::Down, false, 10, size).Active == 99);

	const HexSelection plain = Moved(At(2, 9), HexMove::Right, false, 10, size);
	CHECK(plain.Anchor == 10);
	CHECK(plain.Active == 10);
	CHECK(Moved(At(0, 0), HexMove::Right, false, 10, 0).Active == 0);
}

TEST_CASE("Shift extends the selection from its anchor")
{
	HexSelection s = Moved(At(20, 20), HexMove::Right, true, 10, 100);
	CHECK(s.Anchor == 20);
	CHECK(s.Active == 21);
	CHECK(s.First() == 20);
	CHECK(s.Count() == 2);

	s = Moved(At(20, 20), HexMove::Up, true, 10, 100);
	CHECK(s.Anchor == 20);
	CHECK(s.Active == 4);
	CHECK(s.First() == 4);
	CHECK(s.Count() == 17);
	CHECK(HexSelection{7, 7}.Count() == 1);
}

TEST_CASE("Page keys move by the visible rows")
{
	CHECK(Moved(At(500, 500), HexMove::PageDown, false, 10, 1000).Active == 660);
	CHECK(Moved(At(500, 500), HexMove::PageUp, false, 10, 1000).Active == 340);
	CHECK(Moved(At(100, 100), HexMove::PageUp, false, 10, 1000).Active == 4);
	CHECK(Moved(At(900, 900), HexMove::PageDown, false, 10, 1000).Active == 999);
	CHECK(Moved(At(50, 50), HexMove::PageDown, false, 0, 1000).Active == 66);
}

TEST_CASE("Home and End go to the row's ends, Ctrl+Home and Ctrl+End to the file's")
{
	CHECK(Moved(At(37, 37), HexMove::RowStart, false, 10, 100).Active == 32);
	CHECK(Moved(At(37, 37), HexMove::RowEnd, false, 10, 100).Active == 47);
	CHECK(Moved(At(97, 97), HexMove::RowEnd, false, 10, 100).Active == 99);
	CHECK(Moved(At(37, 37), HexMove::FileStart, false, 10, 100).Active == 0);
	CHECK(Moved(At(37, 37), HexMove::FileEnd, false, 10, 100).Active == 99);
	CHECK(Moved(At(37, 37), HexMove::FileEnd, true, 10, 100).Anchor == 37);
}

TEST_CASE("A selection is clamped when the file gets shorter")
{
	const HexSelection s = Clamped(At(80, 95), 50);
	CHECK(s.Anchor == 49);
	CHECK(s.Active == 49);
	CHECK(Clamped(At(3, 4), 50).Active == 4);
	CHECK(Clamped(At(3, 4), 0).Active == 0);
	CHECK(Clamped(At(3, 4), 0).Count() == 1);
}

TEST_CASE("Scrolling stops at the last screenful")
{
	CHECK(ScrolledBy({0}, 5, 100, 10).TopRow == 5);
	CHECK(ScrolledBy({85}, 20, 100, 10).TopRow == 90);
	CHECK(ScrolledBy({3}, -20, 100, 10).TopRow == 0);
	CHECK(ScrolledBy({3}, INT64_MIN, 100, 10).TopRow == 0);
	CHECK(ScrolledBy({3}, INT64_MAX, 100, 10).TopRow == 90);
	CHECK(ScrolledBy({3}, 5, 8, 10).TopRow == 0);
	CHECK(ScrolledBy({50}, 0, 20, 10).TopRow == 10);
}

TEST_CASE("Revealing a row scrolls the least that shows it")
{
	CHECK(Revealing({10}, 15, 10, 100).TopRow == 10);
	CHECK(Revealing({10}, 10, 10, 100).TopRow == 10);
	CHECK(Revealing({10}, 19, 10, 100).TopRow == 10);
	CHECK(Revealing({10}, 20, 10, 100).TopRow == 11);
	CHECK(Revealing({10}, 4, 10, 100).TopRow == 4);
	CHECK(Revealing({10}, 99, 10, 100).TopRow == 90);
}

TEST_CASE("The grab reaches the last row of a 5 GiB file exactly")
{
	constexpr std::uint64_t rowCount = 5ull * 1024 * 1024 * 1024 / 16;
	constexpr std::uint64_t visible = 40;
	constexpr float track = 800.0f;

	const ScrollGrab top = GrabFor({0}, rowCount, visible, track, 24.0f);
	CHECK(top.Top == 0.0f);
	CHECK(top.Height == 24.0f);

	const HexScroll last{rowCount - visible};
	const ScrollGrab bottom = GrabFor(last, rowCount, visible, track, 24.0f);
	CHECK(bottom.Top + bottom.Height == doctest::Approx(track));

	CHECK(ScrollForGrabTop(bottom.Top, rowCount, visible, track, bottom.Height).TopRow ==
		  rowCount - visible);
	CHECK(ScrollForGrabTop(track, rowCount, visible, track, 24.0f).TopRow == rowCount - visible);
	CHECK(ScrollForGrabTop(0.0f, rowCount, visible, track, 24.0f).TopRow == 0);
	CHECK(ScrollForGrabTop(-5.0f, rowCount, visible, track, 24.0f).TopRow == 0);

	const std::uint64_t half =
		ScrollForGrabTop((track - 24.0f) / 2, rowCount, visible, track, 24.0f).TopRow;
	CHECK(half == (rowCount - visible) / 2);

	CHECK(GrabFor({0}, 5, 10, track, 24.0f).Height == track);
}

TEST_CASE("A grab never gets smaller than its minimum")
{
	CHECK(GrabFor({0}, 1000000, 10, 500.0f, 20.0f).Height == 20.0f);
	CHECK(GrabFor({0}, 100, 50, 500.0f, 20.0f).Height == doctest::Approx(250.0f));
	CHECK(GrabFor({0}, 100, 50, 10.0f, 20.0f).Height == 10.0f);
}
