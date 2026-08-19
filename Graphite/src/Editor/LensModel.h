#pragma once

#include "Editor/LensRows.h"
#include "Lang/Api/LanguageService.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Core {

	class RowMap;
	class TextBuffer;
}

namespace Graphite {

	// A reference count drawn above a declaration. DocumentRow is Core::RowMap's world - the
	// document row the lens is drawn immediately above, following LensRows's own convention.
	// Column is a visual column in cells, not the declaration's protocol column: it exists so
	// the lens' text lines up with an indented declaration, and getting from one to the other
	// needs the line it belongs to and the tab width, which only SetDeclarations has in hand.
	struct Lens
	{
		std::size_t DocumentRow = 0;

		// Where to DRAW: a visual column in cells, so a lens over an indented declaration lines
		// up with it. Derived from the declaration's start, and useless for anything else.
		std::size_t Column = 0;

		// What to ASK ABOUT: the declaration's name, carried in the protocol's own units so it
		// reaches the server untouched. Kept beside the drawing column rather than derived from
		// it - a column converted from cells back to code units lands on the declaration's start,
		// which for a constructor is its own class's name and answers about the type instead.
		std::size_t NameLine = 0;
		Utf16Column NameColumn;

		std::size_t Count = 0;
		bool CountKnown = false;
	};

	// A count that survived its document, paired with the identity it belongs to. Name and kind
	// rather than a row, for the same reason SetDeclarations carries counts over that way: a row
	// only means something against the buffer it was measured in, and this outlives that buffer.
	struct KnownCount
	{
		std::string Name;
		DeclarationKind Kind = DeclarationKind::Other;
		std::size_t Count = 0;
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
		// Counts are carried over by declaration identity (name and kind), not by row: a row
		// surviving the rebuild does not mean the same declaration still owns it (see the .cpp for
		// why row-keying was wrong). Only a declaration whose name and kind match one already known
		// keeps its count; anything else starts out unknown, even if it landed on a row that used
		// to have an answer.
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

		// Answers worth keeping past the model's own life - the counts that came back, and nothing
		// about the ones still outstanding. What a closed document leaves behind.
		std::vector<KnownCount> KnownCounts() const;

		// Counts from a previous life of this document, applied to whatever declarations arrive
		// next and matching by the same identity SetDeclarations carries over on. A name nothing
		// answers to is dropped rather than kept waiting.
		//
		// Used once. After the next SetDeclarations the counts live in the entries themselves and
		// carry over normally; keeping the seed beyond that would let a number the server has
		// since revised come back every time the declarations were rebuilt.
		void SeedCounts(std::vector<KnownCount> counts);

	private:
		// The declaration's name and kind travel with its Lens so the next SetDeclarations can
		// carry a count over by identity rather than by row - see SetDeclarations's comment.
		struct Entry
		{
			Lens LensValue;
			std::string Name;
			DeclarationKind Kind = DeclarationKind::Other;
		};

		static bool ByDocumentRow(const Entry& a, const Entry& b);

		// Sorted by LensValue.DocumentRow, unique - kept in step with m_Rows, which exists only to
		// answer screen-row questions and carries no counts of its own.
		std::vector<Entry> m_Entries;
		LensRows m_Rows;

		// Empty except between a restore and the first reply that follows it.
		std::vector<KnownCount> m_Seeded;
	};
}
