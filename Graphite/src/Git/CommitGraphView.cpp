#include "Git/CommitGraphView.h"

#include <imgui.h>

#include <algorithm>

namespace Graphite {

	void CommitGraphView::Render(const Core::CommitGraph& graph, const GitPalette& palette,
								 const GraphMetrics& metrics, int firstRow, int lastRow)
	{
		if (graph.Nodes.empty())
		{
			return;
		}

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();

		const auto centre = [&](std::size_t row, std::size_t lane) {
			return ImVec2{origin.x + LaneCentreX(metrics, lane),
						  origin.y + (static_cast<float>(row) + 0.5f) * metrics.RowHeight};
		};

		for (const Core::GraphEdge& edge : graph.Edges)
		{
			// An edge wholly above or below the drawn range contributes nothing.
			if (static_cast<int>(edge.ToRow) < firstRow || static_cast<int>(edge.FromRow) > lastRow)
			{
				continue;
			}

			const ImVec2 from = centre(edge.FromRow, edge.FromLane);
			const ImVec2 to = centre(edge.ToRow, edge.ToLane);
			const ImU32 colour = palette.LaneOf(edge.ToLane);

			if (ShapeOf(edge) == EdgeShape::Straight)
			{
				draw->AddLine(from, to, colour, 1.5f);
				continue;
			}

			// The sideways move happens on the source row, not on the destination row.
			// A lane is released at the child's row, so an unrelated branch may occupy the
			// source lane from the very next row down — a line that ran down the source
			// lane first would be drawn straight through it. GraphLayout.h documents this.
			const ImVec2 corner = ImVec2{to.x, from.y};
			draw->AddLine(from, corner, colour, 1.5f);
			draw->AddLine(corner, to, colour, 1.5f);
		}

		for (const Core::GraphNode& node : graph.Nodes)
		{
			if (static_cast<int>(node.Row) < firstRow || static_cast<int>(node.Row) > lastRow)
			{
				continue;
			}

			draw->AddCircleFilled(centre(node.Row, node.Lane), metrics.DotRadius,
								  palette.LaneOf(node.Lane));
		}
	}
}
