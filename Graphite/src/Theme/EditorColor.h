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

		// Every match of the current find, and the one the caret is on. Two colours, because a
		// counter reading "3 / 17" is only useful when the other sixteen are visible and the
		// third is distinguishable among them.
		FindMatch,
		FindCurrentMatch,

		// The reference count drawn on its own row above a declaration. Dimmed: it is a note
		// about the code rather than code, and reading past it must cost nothing.
		Lens,
		Count
	};
}
