#include "Editor/Overlays/LensRows.h"

#include <algorithm>

namespace Graphite {

	void LensRows::SetAnchors(std::vector<std::size_t> anchors)
	{
		std::sort(anchors.begin(), anchors.end());
		anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
		m_Anchors = std::move(anchors);
	}

	std::size_t LensRows::TotalScreenRows(std::size_t documentRowCount) const
	{
		return documentRowCount + m_Anchors.size();
	}

	LensRows::Row LensRows::At(std::size_t screenRow) const
	{
		// Anchor k's lens is drawn at screen row anchors[k] + k: k lenses already sit above it,
		// each having pushed the document rows below it down by one screen row. That quantity is
		// strictly increasing in k because anchors is strictly increasing, so a binary search over
		// the index finds the first k whose lens is at or past screenRow.
		std::size_t lo = 0;
		std::size_t hi = m_Anchors.size();

		while (lo < hi)
		{
			const std::size_t mid = lo + (hi - lo) / 2;

			if (m_Anchors[mid] + mid < screenRow)
			{
				lo = mid + 1;
			}
			else
			{
				hi = mid;
			}
		}

		if (lo < m_Anchors.size() && m_Anchors[lo] + lo == screenRow)
		{
			return Row{true, m_Anchors[lo]};
		}

		// screenRow is a document row: lo lenses sit above it, each having consumed one screen
		// row that the document row itself does not occupy.
		return Row{false, screenRow - lo};
	}

	std::size_t LensRows::ScreenRowOf(std::size_t documentRow) const
	{
		// Every anchor at or before documentRow has already inserted its lens above documentRow's
		// own screen row, so the count of anchors <= documentRow is exactly how far documentRow
		// has been pushed down.
		const auto it = std::upper_bound(m_Anchors.begin(), m_Anchors.end(), documentRow);
		const std::size_t pushedDown = static_cast<std::size_t>(std::distance(m_Anchors.begin(), it));

		return documentRow + pushedDown;
	}

	void LensRows::ShiftAnchors(std::ptrdiff_t delta, std::size_t fromRow)
	{
		std::vector<std::size_t> shifted;
		shifted.reserve(m_Anchors.size());

		for (std::size_t anchor : m_Anchors)
		{
			if (anchor < fromRow)
			{
				shifted.push_back(anchor);
				continue;
			}

			const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(anchor) + delta;

			// A negative shift can push an anchor at or below fromRow up past it; that anchor no
			// longer corresponds to anything sensible until the server's next reply replaces this
			// stopgap, so it is dropped rather than clamped.
			if (moved < static_cast<std::ptrdiff_t>(fromRow))
			{
				continue;
			}

			shifted.push_back(static_cast<std::size_t>(moved));
		}

		std::sort(shifted.begin(), shifted.end());
		m_Anchors = std::move(shifted);
	}
}
