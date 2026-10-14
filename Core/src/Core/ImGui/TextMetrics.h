#pragma once

#include <string_view>

namespace Core {

	float MonospaceCellWidth();

	float TextWidth(std::string_view text);

	struct GlyphInk
	{
		float StartFromDrawX = 0.0f;
		float EndFromDrawX = 0.0f;
	};

	GlyphInk MeasureGlyphInk(std::string_view character);
}
