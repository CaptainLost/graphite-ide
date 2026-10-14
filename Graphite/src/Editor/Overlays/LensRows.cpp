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
		std::size_t lowIndex = 0;
		std::size_t highIndex = m_Anchors.size();

		while (lowIndex < highIndex)
		{
			const std::size_t middleIndex = lowIndex + (highIndex - lowIndex) / 2;

			if (m_Anchors[middleIndex] + middleIndex < screenRow)
			{
				lowIndex = middleIndex + 1;
			}
			else
			{
				highIndex = middleIndex;
			}
		}

		if (lowIndex < m_Anchors.size() && m_Anchors[lowIndex] + lowIndex == screenRow)
		{
			const bool isLens = true;

			return Row{isLens, m_Anchors[lowIndex]};
		}

		const bool isLens = false;

		return Row{isLens, screenRow - lowIndex};
	}

	std::size_t LensRows::ScreenRowOf(std::size_t documentRow) const
	{
		const std::vector<std::size_t>::const_iterator firstAfter =
			std::upper_bound(m_Anchors.begin(), m_Anchors.end(), documentRow);
		const std::size_t pushedDown = static_cast<std::size_t>(std::distance(m_Anchors.begin(), firstAfter));

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
