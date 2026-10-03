#pragma once

#include "Theme/GitPalette.h"
#include "Git/History/GraphGeometry.h"

#include "Core/Git/GraphLayout.h"

#include <imgui.h>

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
		// the range are still drawn, clipped by whatever clip rect is current, so a line
		// entering from above does not appear out of nowhere.
		//
		// `origin` is the screen position of the top-left of display row 0, which may be far
		// above the visible part of the list. Handed in rather than read from the cursor:
		// the list draws this from inside a table cell, where moving the cursor to row 0 and
		// submitting nothing after it is what ImGui asserts on.
		void Render(const Core::CommitGraph& graph, const GitPalette& palette,
					const GraphMetrics& metrics, const std::vector<std::size_t>& rowOfCommit,
					int firstRow, int lastRow, ImVec2 origin);
	};
}
