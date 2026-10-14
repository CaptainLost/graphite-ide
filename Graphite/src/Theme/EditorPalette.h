#pragma once

#include <imgui.h>

namespace Graphite {

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
		ImU32 Lens = 0;
		ImU32 MatchingBracket = 0;
		ImU32 UnmatchedBracket = 0;
		ImU32 DiagnosticError = 0;
		ImU32 DiagnosticWarning = 0;

		ImU32 ChangeAdded = 0;
		ImU32 ChangeModified = 0;
		ImU32 ChangeRemoved = 0;

		ImU32 ChangeOldText = 0;
		ImU32 ChangeOldRemoved = 0;
	};
}
