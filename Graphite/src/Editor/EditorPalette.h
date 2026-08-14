#pragma once

#include <imgui.h>

namespace Graphite {

	// Everything the editor draws that is not a token. Named fields rather than an array
	// indexed by EditorColor, because nothing indexes it — the one place that maps the two
	// together is the palette builder, and ten explicit assignments there are easier to
	// check than a loop.
	struct EditorPalette
	{
		ImU32 Background = 0;
		ImU32 Text = 0;
		ImU32 Number = 0;
		ImU32 Selection = 0;
		ImU32 CurrentLine = 0;
		ImU32 Guide = 0;
		ImU32 GutterFill = 0;
		ImU32 Caret = 0;
		ImU32 FindMatch = 0;
		ImU32 FindCurrentMatch = 0;
	};
}
