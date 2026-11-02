#pragma once

#include <cstdint>

namespace Graphite {

	enum class EditorColor : std::uint8_t
	{
		Background,
		Text,
		Selection,
		SelectionOccurrence,
		CurrentLine,
		Guide,
		GutterFill,
		LineNumber,
		CurrentLineNumber,
		Caret,

		FindMatch,
		FindCurrentMatch,

		Lens,
		LineAuthor,

		MatchingBracket,
		UnmatchedBracket,

		DiagnosticError,
		DiagnosticWarning,

		Count
	};
}
