#pragma once

#include "DrawnGlyphs.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// One line of text a draw list holds, as DrawnTextLines reads it back.
struct DrawnLine
{
	float Top = 0.0f; // the line's top, in screen space
	std::string Text; // its characters left to right, without spaces
};

// The ASCII text a draw list holds in one baked font, read back from its glyph quads line by line, top to bottom. A space
// draws no quad, so "In bases" reads "Inbases"; a glyph of another font or size, or one cut by a clip rectangle, is not read.
// WARNING: `baked` must be the font the text was drawn in, taken inside a frame that drew it.
inline std::vector<DrawnLine> DrawnTextLines(const ImDrawList& drawing, const ImFontBaked& baked)
{
	constexpr unsigned int kFirstVisibleAscii = '!';
	constexpr unsigned int kLastVisibleAscii = '~';

	std::map<std::pair<float, float>, const ImFontGlyph*> glyphAtCorner;

	for (const ImFontGlyph& glyph : baked.Glyphs)
	{
		if (glyph.Visible && glyph.Codepoint >= kFirstVisibleAscii && glyph.Codepoint <= kLastVisibleAscii)
		{
			glyphAtCorner[{glyph.U0, glyph.V0}] = &glyph;
		}
	}

	struct Placed
	{
		float LineTop = 0.0f;
		float Left = 0.0f;
		char Character = ' ';
	};

	std::vector<Placed> placed;

	for (const DrawnGlyph& drawn : DrawnGlyphs(drawing))
	{
		const auto found = glyphAtCorner.find({drawn.Uv.x, drawn.Uv.y});

		if (found == glyphAtCorner.end())
		{
			continue;
		}

		const ImFontGlyph& glyph = *found->second;
		const float scale = glyph.Y1 > glyph.Y0 ? drawn.Box.GetHeight() / (glyph.Y1 - glyph.Y0) : 1.0f;
		placed.push_back(Placed{.LineTop = drawn.Box.Min.y - glyph.Y0 * scale,
								.Left = drawn.Box.Min.x,
								.Character = static_cast<char>(glyph.Codepoint)});
	}

	std::ranges::sort(placed, {}, &Placed::LineTop);

	// Glyphs of one line start within half a line of each other; a pill's text sits a pixel or two off its line's top.
	const float sameLine = baked.Size * 0.5f;
	std::vector<std::vector<Placed>> lines;

	for (const Placed& glyph : placed)
	{
		if (lines.empty() || glyph.LineTop - lines.back().front().LineTop > sameLine)
		{
			lines.emplace_back();
		}

		lines.back().push_back(glyph);
	}

	std::vector<DrawnLine> read;

	for (std::vector<Placed>& line : lines)
	{
		std::ranges::sort(line, {}, &Placed::Left);

		DrawnLine text{.Top = line.front().LineTop};

		for (const Placed& glyph : line)
		{
			text.Text += glyph.Character;
		}

		read.push_back(std::move(text));
	}

	return read;
}

// The index of the first line that starts with `start`, written without spaces.
inline std::optional<std::size_t> LineStartingWith(const std::vector<DrawnLine>& lines, std::string_view start)
{
	for (std::size_t index = 0; index < lines.size(); ++index)
	{
		if (lines[index].Text.starts_with(start))
		{
			return index;
		}
	}

	return std::nullopt;
}
