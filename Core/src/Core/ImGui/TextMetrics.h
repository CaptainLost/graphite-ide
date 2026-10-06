#pragma once

#include <string_view>

namespace Core {

	// The width of one character cell in the monospaced face currently pushed.
	//
	// Taken from the baked font rather than from ImGui::CalcTextSize, which ends with
	// `text_size.x = ImCeilFast(text_size.x)` and so hands back the advance rounded up to a
	// whole pixel. ImDrawList::AddText advances by the true fractional value, so a rounded cell
	// width drifts by ceil(advance) - advance per column against the glyphs it is supposed to
	// line up with - several pixels by column 40, enough to put a caret, a selection edge or a
	// mouse hit test each a character away from the text.
	float MonospaceCellWidth();

	float TextWidth(std::string_view text);

	struct GlyphInk
	{
		float StartFromDrawX = 0.0f;
		float EndFromDrawX = 0.0f;
	};

	// Icon glyphs carry empty margins of differing widths inside their advance, so glyphs
	// spaced by advance look unevenly spaced.
	GlyphInk MeasureGlyphInk(std::string_view character);
}
