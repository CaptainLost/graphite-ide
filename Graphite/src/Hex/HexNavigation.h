#pragma once

#include <cstdint>

namespace Graphite {

	struct HexSelection
	{
		std::uint64_t Anchor = 0;
		std::uint64_t Active = 0;

		// The first selected byte, and how many are selected; both ends are included.
		std::uint64_t First() const;
		std::uint64_t Count() const;
	};

	enum class HexMove
	{
		Left,
		Right,
		Up,
		Down,
		PageUp,
		PageDown,
		RowStart,
		RowEnd,
		FileStart,
		FileEnd
	};

	// Moves are clamped to [0, fileSize - 1]; without `extend` the anchor follows the active byte.
	HexSelection Moved(HexSelection selection, HexMove move, bool extend, std::uint64_t visibleRows,
					   std::uint64_t fileSize);

	HexSelection Clamped(HexSelection selection, std::uint64_t fileSize);

	// The view keeps its own row, because a float scroll cannot address the rows of a multi-gigabyte file.
	struct HexScroll
	{
		std::uint64_t TopRow = 0;
	};

	// Clamped to [0, max(0, rowCount - visibleRows)].
	HexScroll ScrolledBy(HexScroll scroll, std::int64_t rows, std::uint64_t rowCount,
						 std::uint64_t visibleRows);

	// The least scroll that shows `row`.
	HexScroll Revealing(HexScroll scroll, std::uint64_t row, std::uint64_t visibleRows,
						std::uint64_t rowCount);

	struct ScrollGrab
	{
		float Top = 0.0f;
		float Height = 0.0f;
	};

	// WARNING: runs in double; a float cannot tell the rows of a 5 GiB file apart. A grab touching the
	// track's bottom maps to exactly the last top row.
	ScrollGrab GrabFor(HexScroll scroll, std::uint64_t rowCount, std::uint64_t visibleRows,
					   float trackHeight, float minimumHeight);

	HexScroll ScrollForGrabTop(float grabTop, std::uint64_t rowCount, std::uint64_t visibleRows,
							   float trackHeight, float grabHeight);
}
