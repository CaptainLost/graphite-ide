#pragma once

#include <cstdint>

namespace Graphite {

	// Everything the editor draws that is not a token. Count is last so an array sized from it
	// grows when a colour is added, rather than leaving an entry nobody filled.
	enum class EditorColor : std::uint8_t
	{
		Background,
		Text,
		Selection,
		CurrentLine,
		Guide,
		GutterFill,
		LineNumber,
		Caret,
		Count
	};
}
