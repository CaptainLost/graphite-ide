#pragma once

#include <cstdint>

namespace Graphite {

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

		FindMatch,
		FindCurrentMatch,

		Lens,

		MatchingBracket,
		UnmatchedBracket,

		DiagnosticError,
		DiagnosticWarning,

		Count
	};
}
