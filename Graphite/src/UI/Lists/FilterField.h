#pragma once

#include <IconsLucide.h>

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	struct FilterFieldLook
	{
		const char* Hint = "";

		const char* Glyph = ICON_LC_SEARCH;

		std::optional<float> Width;

		// Receives the id of the text input itself, not of the clear button beside it.
		unsigned int* InputId = nullptr;
	};

	bool DrawFilterField(const char* id, char* buffer, std::size_t bufferSize, const FilterFieldLook& look);
	bool DrawFilterField(const char* id, std::string& text, const FilterFieldLook& look);

	float FilterFieldWidthBefore(int iconButtons);
}
