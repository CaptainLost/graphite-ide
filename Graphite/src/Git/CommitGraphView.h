#pragma once

#include "Git/GitPalette.h"
#include "Git/GraphGeometry.h"

#include "Core/Git/GraphLayout.h"

#include <cstddef>
#include <vector>

namespace Graphite {

	// Draws the lane column beside a list of commits. Stateless: the caller owns the graph,
	// the scroll position and the row range, because the list beside it already knows all
	// three and two sources of truth would drift apart by a row.
	class CommitGraphView
	{
	public:
		// rowOfCommit maps a commit's index in the graph to its display row — with files
		// interleaved between commits, those are no longer the same number. firstRow and
		// lastRow bound the display rows the list is drawing this frame. Edges that leave
		// the range are still drawn, clipped by the child window, so a line entering from
		// above does not appear out of nowhere.
		void Render(const Core::CommitGraph& graph, const GitPalette& palette,
					const GraphMetrics& metrics, const std::vector<std::size_t>& rowOfCommit,
					int firstRow, int lastRow);
	};
}
