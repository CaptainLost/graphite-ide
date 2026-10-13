#pragma once

#include <imgui.h>

#include <algorithm>
#include <cfloat>

inline bool DrawsInColour(const ImDrawList& drawing, ImU32 colour)
{
	for (const ImDrawVert& vertex : drawing.VtxBuffer)
	{
		if (vertex.col == colour)
		{
			return true;
		}
	}

	return false;
}

// A selected row is drawn in the header colour only while the mouse is off it.
inline bool MarksARow(const ImDrawList& drawing)
{
	return DrawsInColour(drawing, ImGui::GetColorU32(ImGuiCol_Header));
}

inline float SideOfEdge(ImVec2 edgeFrom, ImVec2 edgeTo, ImVec2 point)
{
	return (edgeTo.x - edgeFrom.x) * (point.y - edgeFrom.y) - (edgeTo.y - edgeFrom.y) * (point.x - edgeFrom.x);
}

inline bool TriangleCovers(ImVec2 first, ImVec2 second, ImVec2 third, ImVec2 point)
{
	const float sideOfFirstEdge = SideOfEdge(first, second, point);
	const float sideOfSecondEdge = SideOfEdge(second, third, point);
	const float sideOfThirdEdge = SideOfEdge(third, first, point);

	const bool leftOfAnEdge = sideOfFirstEdge < 0.0f || sideOfSecondEdge < 0.0f || sideOfThirdEdge < 0.0f;
	const bool rightOfAnEdge = sideOfFirstEdge > 0.0f || sideOfSecondEdge > 0.0f || sideOfThirdEdge > 0.0f;
	return !(leftOfAnEdge && rightOfAnEdge);
}

// Whether the one shape drawn in `colour` is solid at its middle: true of a filled dot, false of
// a ring, and false when nothing is drawn in it.
inline bool FillsMiddleInColour(const ImDrawList& drawing, ImU32 colour)
{
	bool foundVertexInColour = false;
	ImVec2 lowest(FLT_MAX, FLT_MAX);
	ImVec2 highest(-FLT_MAX, -FLT_MAX);

	for (const ImDrawVert& vertex : drawing.VtxBuffer)
	{
		if (vertex.col == colour)
		{
			foundVertexInColour = true;
			lowest = ImVec2(std::min(lowest.x, vertex.pos.x), std::min(lowest.y, vertex.pos.y));
			highest = ImVec2(std::max(highest.x, vertex.pos.x), std::max(highest.y, vertex.pos.y));
		}
	}

	if (!foundVertexInColour)
	{
		return false;
	}

	const ImVec2 middle((lowest.x + highest.x) * 0.5f, (lowest.y + highest.y) * 0.5f);

	for (const ImDrawCmd& command : drawing.CmdBuffer)
	{
		for (unsigned int cornerIndex = 0; cornerIndex + 2 < command.ElemCount; cornerIndex += 3)
		{
			const ImDrawVert& first = drawing.VtxBuffer[command.VtxOffset + drawing.IdxBuffer[command.IdxOffset + cornerIndex]];
			const ImDrawVert& second = drawing.VtxBuffer[command.VtxOffset + drawing.IdxBuffer[command.IdxOffset + cornerIndex + 1]];
			const ImDrawVert& third = drawing.VtxBuffer[command.VtxOffset + drawing.IdxBuffer[command.IdxOffset + cornerIndex + 2]];

			const bool solidInColour = first.col == colour && second.col == colour && third.col == colour;

			if (solidInColour && TriangleCovers(first.pos, second.pos, third.pos, middle))
			{
				return true;
			}
		}
	}

	return false;
}
