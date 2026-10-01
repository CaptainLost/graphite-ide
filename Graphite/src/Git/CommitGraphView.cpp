#include "Git/CommitGraphView.h"

#include "Core/ImGui/UiScale.h"

#include <imgui.h>

#include <algorithm>
#include <optional>

namespace Graphite {

	void CommitGraphView::Render(const Core::CommitGraph& graph, const GitPalette& palette,
								 const GraphMetrics& metrics, const std::vector<std::size_t>& rowOfCommit,
								 int firstRow, int lastRow, ImVec2 origin)
	{
		if (graph.Nodes.empty())
		{
			return;
		}

		ImDrawList* draw = ImGui::GetWindowDrawList();

		const auto centre = [&](std::size_t row, std::size_t lane) {
			return ImVec2{origin.x + LaneCentreX(metrics, lane), origin.y + RowCentreY(metrics, row)};
		};

		// A commit's display row, or nothing when the layout and the mapping disagree
		// about how many commits there are — which means the caller built one from a page
		// and the other from a different one.
		const auto displayRow = [&rowOfCommit](std::size_t commit) -> std::optional<std::size_t> {
			if (commit >= rowOfCommit.size())
			{
				return std::nullopt;
			}

			return rowOfCommit[commit];
		};

		for (const Core::GraphEdge& edge : graph.Edges)
		{
			const std::optional<std::size_t> fromRow = displayRow(edge.FromRow);
			const std::optional<std::size_t> toRow = displayRow(edge.ToRow);

			if (!fromRow.has_value() || !toRow.has_value())
			{
				continue;
			}

			// An edge wholly above or below the drawn range contributes nothing.
			if (static_cast<int>(*toRow) < firstRow || static_cast<int>(*fromRow) > lastRow)
			{
				continue;
			}

			const ImVec2 from = centre(*fromRow, edge.FromLane);
			const ImVec2 to = centre(*toRow, edge.ToLane);

			// A connector belongs to the branch that occupies the outer lane: for a fork
			// that is the side branch's tip, for a merge it is the branch reaching back to
			// its base, and for a straight edge the two lanes agree anyway.
			const ImU32 colour = palette.LaneOf(std::max(edge.FromLane, edge.ToLane));

			if (ShapeOf(edge) == EdgeShape::Straight)
			{
				draw->AddLine(from, to, colour, Core::Scaled(1.5f));
				continue;
			}

			// The sideways move happens on the source row, not on the destination row.
			// A lane is released at the child's row, so an unrelated branch may occupy the
			// source lane from the very next row down — a line that ran down the source
			// lane first would be drawn straight through it. GraphLayout.h documents this.
			const ImVec2 corner = ImVec2{to.x, from.y};
			draw->AddLine(from, corner, colour, Core::Scaled(1.5f));
			draw->AddLine(corner, to, colour, Core::Scaled(1.5f));
		}

		for (const Core::GraphNode& node : graph.Nodes)
		{
			const std::optional<std::size_t> row = displayRow(node.Row);

			if (!row.has_value())
			{
				continue;
			}

			if (static_cast<int>(*row) < firstRow || static_cast<int>(*row) > lastRow)
			{
				continue;
			}

			draw->AddCircleFilled(centre(*row, node.Lane), metrics.DotRadius,
								  palette.LaneOf(node.Lane));
		}
	}
}
