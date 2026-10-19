#pragma once

#include "UI/Lists/ListRow.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <initializer_list>
#include <optional>

inline int ChannelOf(ImU32 colour, int shift)
{
	return static_cast<int>((colour >> shift) & 0xFFu);
}

// Each colour channel within one level of the other's, the rounding a share of a lerp can land on.
inline bool WithinOneLevel(ImU32 colour, ImU32 expected)
{
	for (const int shift : {IM_COL32_R_SHIFT, IM_COL32_G_SHIFT, IM_COL32_B_SHIFT})
	{
		if (std::abs(ChannelOf(colour, shift) - ChannelOf(expected, shift)) > 1)
		{
			return false;
		}
	}

	return true;
}

// How far `colour` lies from `from` toward `to`, read on the channel where the two differ most.
inline float ShareFromTo(ImU32 colour, ImU32 from, ImU32 to)
{
	int widestShift = IM_COL32_R_SHIFT;

	for (const int shift : {IM_COL32_G_SHIFT, IM_COL32_B_SHIFT})
	{
		if (std::abs(ChannelOf(to, shift) - ChannelOf(from, shift)) >
			std::abs(ChannelOf(to, widestShift) - ChannelOf(from, widestShift)))
		{
			widestShift = shift;
		}
	}

	const float span = static_cast<float>(ChannelOf(to, widestShift) - ChannelOf(from, widestShift));
	return static_cast<float>(ChannelOf(colour, widestShift) - ChannelOf(from, widestShift)) / span;
}

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

struct HorizontalExtent
{
	float Left = FLT_MAX;
	float Right = -FLT_MAX;
};

inline std::optional<HorizontalExtent> HorizontalExtentInColour(const ImDrawList& drawing, ImU32 colour)
{
	HorizontalExtent extent;
	bool foundVertexInColour = false;

	for (const ImDrawVert& vertex : drawing.VtxBuffer)
	{
		if (vertex.col == colour)
		{
			foundVertexInColour = true;
			extent.Left = std::min(extent.Left, vertex.pos.x);
			extent.Right = std::max(extent.Right, vertex.pos.x);
		}
	}

	if (!foundVertexInColour)
	{
		return std::nullopt;
	}

	return extent;
}

struct VerticalExtent
{
	float Top = FLT_MAX;
	float Bottom = -FLT_MAX;
};

inline std::optional<VerticalExtent> VerticalExtentInColour(const ImDrawList& drawing, ImU32 colour)
{
	VerticalExtent extent;
	bool foundVertexInColour = false;

	for (const ImDrawVert& vertex : drawing.VtxBuffer)
	{
		if (vertex.col == colour)
		{
			foundVertexInColour = true;
			extent.Top = std::min(extent.Top, vertex.pos.y);
			extent.Bottom = std::max(extent.Bottom, vertex.pos.y);
		}
	}

	if (!foundVertexInColour)
	{
		return std::nullopt;
	}

	return extent;
}

inline int VerticesInColour(const ImDrawList& drawing, ImU32 colour)
{
	int count = 0;

	for (const ImDrawVert& vertex : drawing.VtxBuffer)
	{
		if (vertex.col == colour)
		{
			++count;
		}
	}

	return count;
}

// A selected row is drawn in the list row's selected colour unless it is held down.
inline bool MarksARow(const ImDrawList& drawing)
{
	return DrawsInColour(drawing, ImGui::GetColorU32(Graphite::CurrentListRowColours().Selected));
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
