#pragma once

#include "Editor/LensRows.h"
#include "Lang/Api/LanguageService.h"

#include <cstddef>
#include <vector>

namespace Core {

	class RowMap;
	class TextBuffer;
}

namespace Graphite {

	// A reference count drawn above a declaration. DocumentRow is Core::RowMap's world - the
	// row the lens sits above, one row before the declaration's own first row. Column is a
	// visual column in cells, not the declaration's protocol column: it exists so the lens'
	// text lines up with an indented declaration, and getting from one to the other needs the
	// line it belongs to and the tab width, which only SetDeclarations has in hand.
	struct Lens
	{
		std::size_t DocumentRow = 0;
		std::size_t Column = 0;
		std::size_t Count = 0;
		bool CountKnown = false;
	};

	// The per-document model behind the reference-count rows: which declarations have a lens,
	// where each one sits, what its count is, and which counts still need asking about.
	//
	// A lens anchors to a document ROW, not a line: a wrapped declaration spans several rows and
	// the lens belongs above the first of them, so every line is turned into a row through
	// Core::RowMap::FirstRowOf. That is also why SetDeclarations must run again whenever the wrap
	// width changes - FirstRowOf returns different rows once it does, even though the
	// declarations themselves have not moved.
	class LensModel
	{
	public:
		// Rebuilds the anchor set from the declarations the server most recently reported, or the
		// wrap width having changed under the same declarations. `buffer` and `tabWidth` are what
		// turn a declaration's protocol column - UTF-16 code units - into the visual column the
		// lens is drawn at: Core::FromUtf16Column needs the line's text to find the byte offset,
		// and Core::ColumnOfByte needs the tab width to turn that into cells.
		//
		// Counts already known for a document row that survives the rebuild are carried over, so a
		// reply does not blank the screen and then fill it in again - only rows that are new, or
		// whose declaration moved to a different row, start out unknown.
		void SetDeclarations(const std::vector<Declaration>& declarations, const Core::RowMap& rows,
							 const Core::TextBuffer& buffer, std::size_t tabWidth);

		// Records the server's answer for the lens anchored at documentRow. A count of zero is
		// still a known count - it says the declaration is unreferenced, not that nobody asked.
		void SetCount(std::size_t documentRow, std::size_t count);

		// Shifts anchors at or below fromLine's first row, and the counts that go with them, to
		// cover the gap between an edit and the server's next reply. This is a stopgap, not a
		// source of truth: it keeps the display from jumping wildly until SetDeclarations runs
		// again with exact anchors, but it does not track edits precisely, so nothing should rely
		// on it for correctness beyond that window.
		void OnLinesChanged(std::size_t fromLine, std::ptrdiff_t delta, const Core::RowMap& rows);

		// Lenses with no known count yet, anchored within [fromRow, toRow) - the range the clipper
		// is currently drawing, plus whatever margin the caller adds. Answers only about that
		// range rather than the whole file, because asking about the whole file would issue one
		// request per declaration in it.
		std::vector<std::size_t> RowsNeedingCounts(std::size_t fromRow, std::size_t toRow) const;

		const LensRows& GetRows() const { return m_Rows; }

		const Lens* LensAt(std::size_t documentRow) const;

	private:
		// Sorted by DocumentRow, unique - kept in step with m_Rows, which exists only to answer
		// screen-row questions and carries no counts of its own.
		std::vector<Lens> m_Lenses;
		LensRows m_Rows;
	};
}
