#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Graphite {

	// Translates between a document row (Core::RowMap's world, where every row is a real line
	// of the buffer) and a screen row (TextEditorWidget's world, where a lens - a reference
	// count drawn above a symbol, for instance - occupies a row of its own that no document
	// line owns). Core::RowMap is never told about these rows; see
	// docs/adr/0486-insert-non-document-rows-above-rowmap-rather-than-inside-it.md for why the
	// translation lives here instead, at the boundary, rather than inside RowMap itself.
	//
	// A lens sits immediately above the document row it anchors to, so an anchor at document
	// row N adds one screen row before N's own row is drawn.
	//
	// A sorted, unique vector and a binary search are enough: lenses in a file number in the
	// hundreds, not the tens of thousands, so there is no need for the Fenwick tree RowMap
	// carries next door for the case that does need one.
	//
	// pageRows in TextEditorWidget.cpp deliberately does NOT consult this class: it is a count
	// of rows to move, not a row to look up, so PageUp/PageDown step by a page-ful of screen
	// rows measured in document rows and overshoot by however many lenses fall in the range
	// crossed - one or two rows in practice. That is intentional, not a bug to fix here.
	class LensRows
	{
	public:
		struct Row
		{
			bool IsLens = false;

			// The document row this screen row belongs to either way: the row it draws, when
			// IsLens is false, or the row its lens anchors to, when IsLens is true.
			std::size_t DocumentRow = 0;
		};

		// Replaces the anchor set outright. Anchors need not arrive sorted or unique; this
		// normalises both.
		void SetAnchors(std::vector<std::size_t> anchors);

		// The screen row count for a document of documentRowCount rows: one extra row per
		// anchor.
		std::size_t TotalScreenRows(std::size_t documentRowCount) const;

		// What screenRow is: a lens, or the document row it draws.
		//
		// Total, not partial: a screenRow at or past TotalScreenRows(documentRowCount) is
		// arithmetically safe - no underflow, no undefined behaviour - and always answers with a
		// document row extrapolated past the end of the document, never with a lens. Both halves
		// of that are relied on. The clipper stays within the bound and never sees the case; the
		// mouse hit test deliberately does not, because a pointer can be below the last row or
		// dragged out of the window entirely, and it hands the raw row over unclamped. What it
		// gets back is a document row past the end, which VisualToCaret clamps exactly as it
		// clamped an out-of-range row before lenses existed - so the pointer lands on the last
		// line, and cannot be reported as having landed on a lens that is not there.
		Row At(std::size_t screenRow) const;

		// The screen row that draws documentRow itself - not its lens, when it has one.
		std::size_t ScreenRowOf(std::size_t documentRow) const;

		// Moves anchors at or below `fromRow` by `delta` rows, for the gap between an edit and
		// the server's next reply. Anchors above are untouched. This is a stopgap, not a source
		// of truth: it keeps the display from jumping wildly until the server answers again with
		// exact anchors, but it does not attempt to track edits precisely, so do not lean on it
		// for correctness beyond that window.
		void ShiftAnchors(std::ptrdiff_t delta, std::size_t fromRow);

	private:
		std::vector<std::size_t> m_Anchors;
	};
}
