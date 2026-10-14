#include "Git/History/CommitGraphView.h"

#include "Core/ImGui/UiScale.h"

#include <imgui.h>

#include <algorithm>
#include <optional>

namespace Graphite {

	namespace {

		constexpr float kConnectorThickness = 1.5f;

		ImVec2 NodeCentre(ImVec2 origin, const GraphMetrics& metrics, std::size_t row, std::size_t lane)
		{
			return ImVec2{origin.x + LaneCentreX(metrics, lane), origin.y + RowCentreY(metrics, row)};
		}

		std::optional<std::size_t> DisplayRowOfCommit(const std::vector<std::size_t>& rowOfCommit, std::size_t commit)
		{
			if (commit >= rowOfCommit.size())
			{
				return std::nullopt;
			}

			return rowOfCommit[commit];
		}

		bool IsRowDrawn(std::size_t row, int firstRow, int lastRow)
		{
			return static_cast<int>(row) >= firstRow && static_cast<int>(row) <= lastRow;
		}
	}

	void CommitGraphView::Render(const Core::CommitGraph& graph, const GitPalette& palette,
								 const GraphMetrics& metrics, const std::vector<std::size_t>& rowOfCommit,
								 int firstRow, int lastRow, ImVec2 origin)
	{
		if (graph.Nodes.empty())
		{
			return;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		for (const Core::GraphEdge& edge : graph.Edges)
		{
			const std::optional<std::size_t> fromRow = DisplayRowOfCommit(rowOfCommit, edge.FromRow);
			const std::optional<std::size_t> toRow = DisplayRowOfCommit(rowOfCommit, edge.ToRow);

			if (!fromRow.has_value() || !toRow.has_value())
			{
				continue;
			}

			const bool edgeIsAboveDrawnRows = static_cast<int>(*toRow) < firstRow;
			const bool edgeIsBelowDrawnRows = static_cast<int>(*fromRow) > lastRow;

			if (edgeIsAboveDrawnRows || edgeIsBelowDrawnRows)
			{
				continue;
			}

			const ImVec2 from = NodeCentre(origin, metrics, *fromRow, edge.FromLane);
			const ImVec2 to = NodeCentre(origin, metrics, *toRow, edge.ToLane);

			const ImU32 colour = palette.LaneOf(std::max(edge.FromLane, edge.ToLane));

			if (ShapeOf(edge) == EdgeShape::Straight)
			{
				drawList->AddLine(from, to, colour, Core::Scaled(kConnectorThickness));
				continue;
			}

			// WARNING: The sideways move happens on the source row, not the destination row. A lane is released at the child's row, so an unrelated branch may take the source lane from the next row down, and a line running down it first would cross that branch.
			const ImVec2 corner = ImVec2{to.x, from.y};
			drawList->AddLine(from, corner, colour, Core::Scaled(kConnectorThickness));
			drawList->AddLine(corner, to, colour, Core::Scaled(kConnectorThickness));
		}

		for (const Core::GraphNode& node : graph.Nodes)
		{
			const std::optional<std::size_t> row = DisplayRowOfCommit(rowOfCommit, node.Row);

			if (!row.has_value())
			{
				continue;
			}

			if (!IsRowDrawn(*row, firstRow, lastRow))
			{
				continue;
			}

			drawList->AddCircleFilled(NodeCentre(origin, metrics, *row, node.Lane), metrics.DotRadius,
									  palette.LaneOf(node.Lane));
		}
	}
}
