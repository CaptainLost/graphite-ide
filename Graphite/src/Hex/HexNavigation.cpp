#include "Hex/HexNavigation.h"

#include "Hex/HexText.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {

		std::uint64_t MaxTopRow(std::uint64_t rowCount, std::uint64_t visibleRows)
		{
			return rowCount > visibleRows ? rowCount - visibleRows : 0;
		}

		// Stays in the column when there is no row above to go to.
		std::uint64_t Before(std::uint64_t at, std::uint64_t bytes)
		{
			return at >= bytes ? at - bytes : at % kBytesPerRow;
		}

		std::uint64_t After(std::uint64_t at, std::uint64_t bytes, std::uint64_t last)
		{
			return bytes > last - at ? last : at + bytes;
		}
	}

	std::uint64_t HexSelection::First() const
	{
		return std::min(Anchor, Active);
	}

	std::uint64_t HexSelection::Count() const
	{
		return std::max(Anchor, Active) - First() + 1;
	}

	HexSelection Moved(HexSelection selection, HexMove move, bool extend, std::uint64_t visibleRows,
					   std::uint64_t fileSize)
	{
		if (fileSize == 0)
		{
			return {};
		}

		const std::uint64_t last = fileSize - 1;
		const std::uint64_t at = std::min(selection.Active, last);
		const std::uint64_t page = std::max<std::uint64_t>(visibleRows, 1) * kBytesPerRow;
		const std::uint64_t rowStart = at - at % kBytesPerRow;

		std::uint64_t target = at;
		switch (move)
		{
		case HexMove::Left:
			target = at > 0 ? at - 1 : 0;
			break;
		case HexMove::Right:
			target = After(at, 1, last);
			break;
		case HexMove::Up:
			target = Before(at, kBytesPerRow);
			break;
		case HexMove::Down:
			target = After(at, kBytesPerRow, last);
			break;
		case HexMove::PageUp:
			target = Before(at, page);
			break;
		case HexMove::PageDown:
			target = After(at, page, last);
			break;
		case HexMove::RowStart:
			target = rowStart;
			break;
		case HexMove::RowEnd:
			target = After(rowStart, kBytesPerRow - 1, last);
			break;
		case HexMove::FileStart:
			target = 0;
			break;
		case HexMove::FileEnd:
			target = last;
			break;
		}

		selection.Active = target;
		if (!extend)
		{
			selection.Anchor = target;
		}
		return Clamped(selection, fileSize);
	}

	HexSelection Clamped(HexSelection selection, std::uint64_t fileSize)
	{
		if (fileSize == 0)
		{
			return {};
		}
		selection.Anchor = std::min(selection.Anchor, fileSize - 1);
		selection.Active = std::min(selection.Active, fileSize - 1);
		return selection;
	}

	HexScroll ScrolledBy(HexScroll scroll, std::int64_t rows, std::uint64_t rowCount,
						 std::uint64_t visibleRows)
	{
		const std::uint64_t maxTop = MaxTopRow(rowCount, visibleRows);
		std::uint64_t top = std::min(scroll.TopRow, maxTop);
		if (rows < 0)
		{
			// Negating INT64_MIN overflows, so count in unsigned.
			const std::uint64_t back = 0 - static_cast<std::uint64_t>(rows);
			top = back > top ? 0 : top - back;
		}
		else
		{
			const auto forward = static_cast<std::uint64_t>(rows);
			top = forward > maxTop - top ? maxTop : top + forward;
		}
		return {top};
	}

	HexScroll Revealing(HexScroll scroll, std::uint64_t row, std::uint64_t visibleRows,
						std::uint64_t rowCount)
	{
		std::uint64_t top = scroll.TopRow;
		if (row < top)
		{
			top = row;
		}
		else if (visibleRows > 0 && row - top >= visibleRows)
		{
			top = row - visibleRows + 1;
		}
		return {std::min(top, MaxTopRow(rowCount, visibleRows))};
	}

	ScrollGrab GrabFor(HexScroll scroll, std::uint64_t rowCount, std::uint64_t visibleRows,
					   float trackHeight, float minimumHeight)
	{
		const std::uint64_t maxTop = MaxTopRow(rowCount, visibleRows);
		if (maxTop == 0)
		{
			return {0.0f, trackHeight};
		}

		const double track = trackHeight;
		const double proportional =
			track * static_cast<double>(visibleRows) / static_cast<double>(rowCount);
		const double height = std::min(track, std::max<double>(minimumHeight, proportional));
		const double fraction =
			static_cast<double>(std::min(scroll.TopRow, maxTop)) / static_cast<double>(maxTop);
		return {static_cast<float>((track - height) * fraction), static_cast<float>(height)};
	}

	HexScroll ScrollForGrabTop(float grabTop, std::uint64_t rowCount, std::uint64_t visibleRows,
							   float trackHeight, float grabHeight)
	{
		const std::uint64_t maxTop = MaxTopRow(rowCount, visibleRows);
		const double travel = static_cast<double>(trackHeight) - static_cast<double>(grabHeight);
		if (maxTop == 0 || travel <= 0.0)
		{
			return {0};
		}

		const double fraction = std::clamp(static_cast<double>(grabTop) / travel, 0.0, 1.0);
		if (fraction >= 1.0)
		{
			return {maxTop};
		}
		const auto row = static_cast<std::uint64_t>(std::llround(fraction * static_cast<double>(maxTop)));
		return {std::min(maxTop, row)};
	}
}
