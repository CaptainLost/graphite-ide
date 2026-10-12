#pragma once

#include <imgui.h>

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
