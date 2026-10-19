#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <vector>

// A glyph as the draw list holds it: one textured rectangle of four vertices.
struct DrawnGlyph
{
	ImRect Box;
	ImU32 Colour = 0;

	// The top-left texture coordinate, which names the glyph of the atlas the rectangle shows.
	ImVec2 Uv;
};

inline bool IsGlyphVertex(const ImDrawVert& vertex)
{
	const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
	return vertex.uv.x != white.x || vertex.uv.y != white.y;
}

inline std::vector<DrawnGlyph> DrawnGlyphs(const ImDrawList& drawing)
{
	constexpr int kVerticesInAGlyph = 4;

	std::vector<DrawnGlyph> glyphs;
	int first = 0;

	while (first + kVerticesInAGlyph <= drawing.VtxBuffer.Size)
	{
		const ImDrawVert* quad = &drawing.VtxBuffer[first];
		const bool allGlyph = IsGlyphVertex(quad[0]) && IsGlyphVertex(quad[1]) && IsGlyphVertex(quad[2]) && IsGlyphVertex(quad[3]);
		const bool isRectangle = quad[0].pos.y == quad[1].pos.y && quad[1].pos.x == quad[2].pos.x &&
								 quad[2].pos.y == quad[3].pos.y && quad[3].pos.x == quad[0].pos.x;

		if (allGlyph && isRectangle)
		{
			glyphs.push_back(DrawnGlyph{.Box = ImRect(quad[0].pos, quad[2].pos), .Colour = quad[0].col, .Uv = quad[0].uv});
			first += kVerticesInAGlyph;
			continue;
		}

		++first;
	}

	return glyphs;
}

inline std::vector<ImRect> GlyphQuads(const ImDrawList& drawing)
{
	std::vector<ImRect> quads;

	for (const DrawnGlyph& glyph : DrawnGlyphs(drawing))
	{
		quads.push_back(glyph.Box);
	}

	return quads;
}
