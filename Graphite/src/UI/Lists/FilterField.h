#pragma once

#include <IconsMaterialDesignIcons.h>

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	struct FilterFieldLook
	{
		const char* Hint = "";

		// What the field narrows by: the magnifier for words, another glyph where it takes something else, as a path.
		const char* Glyph = ICON_MDI_MAGNIFY;

		// The rest of the line when not given.
		std::optional<float> Width;
	};

	// The one look of a field that narrows a list: the glyph dim inside the field's start, the hint after it, a clear
	// button inside its right end once there is text, and Esc to empty it. The panel's own buttons go after it,
	// outside the field, so give it the width FilterFieldWidthBefore leaves. True when the text changed.
	bool DrawFilterField(const char* id, char* buffer, std::size_t bufferSize, const FilterFieldLook& look);
	bool DrawFilterField(const char* id, std::string& text, const FilterFieldLook& look);

	// The rest of the line, less room for `iconButtons` icon buttons after the field.
	float FilterFieldWidthBefore(int iconButtons);
}
