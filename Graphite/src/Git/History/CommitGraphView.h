#pragma once

#include "Theme/GitPalette.h"
#include "Git/History/GraphGeometry.h"

#include "Core/Git/GraphLayout.h"

#include <imgui.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace Graphite {

	class CommitGraphView
	{
	public:
		// WARNING: origin is the screen position of display row 0, which may be far above the visible list. It is handed in because moving the cursor to row 0 inside a table cell and submitting nothing after it asserts in ImGui.
		// `alphaOfCommit` says how strongly a commit's dot is drawn, and the edges that end at it: 1 for full strength. Asked only for what is drawn.
		using AlphaOfCommit = std::function<float(std::size_t commit)>;

		void Render(const Core::CommitGraph& graph, const GitPalette& palette,
					const GraphMetrics& metrics, const std::vector<std::size_t>& rowOfCommit,
					int firstRow, int lastRow, ImVec2 origin, const AlphaOfCommit& alphaOfCommit = {});
	};
}
