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

		// The pair the caret is sitting on, and a bracket that has no partner. Two colours,
		// because "these belong together" and "this one is wrong" are opposite messages.
		MatchingBracket,
		UnmatchedBracket,

		// The squiggle under what a language server called wrong. Two colours, not four: a
		// diagnostic the buffer has outrun is drawn in the same colour at lower alpha, because
		// stale is a state of a diagnostic rather than a different kind of one - and every
		// colour added here is one every scheme's author has to choose.
		DiagnosticError,
		DiagnosticWarning,

		Count
	};
}
