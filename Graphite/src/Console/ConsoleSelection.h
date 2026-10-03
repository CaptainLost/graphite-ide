#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// A place in the console: which message, and how many characters into it.
	//
	// The message is named by its sequence number rather than by its row, because the console
	// is a window onto a buffer that drops its oldest entries and onto a filter that hides some
	// of what is left: a row number means a different message a moment later, a sequence number
	// means the same one or none at all.
	struct ConsoleAnchor
	{
		std::uint64_t Sequence = 0;
		std::size_t Column = 0;
	};

	// Half-open, in columns of the message it was asked about.
	struct ConsoleColumnSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;
	};

	// Where the console's rows sit on screen, in the units ImGui draws them with. The origin is
	// the top left of the first row's text.
	struct ConsoleLayout
	{
		float OriginX = 0.0f;
		float OriginY = 0.0f;
		float RowHeight = 0.0f;
		float CellWidth = 0.0f;
	};

	// The place in `entries` that a point on screen falls on: the row it is over and the
	// character boundary nearest to it, which is what puts the edge of a sweep between two
	// characters rather than always before the one under the pointer.
	//
	// A point above the rows lands at the start of the first entry and one below them at the
	// end of the last, so dragging out of the panel keeps selecting rather than stopping.
	// Nothing comes back when there are no entries to land on.
	std::optional<ConsoleAnchor> AnchorAtPoint(const std::vector<Core::LogEntry>& entries,
											   const ConsoleLayout& layout, float x, float y);

	// What the mouse has swept over in the console, and what that comes to as text.
	//
	// Holds no messages of its own: the entries are handed in when the text is wanted, so a
	// selection cannot go stale against a buffer that has moved on underneath it.
	class ConsoleSelection
	{
	public:
		// Puts both ends on `at` - a press that has not been dragged anywhere yet selects
		// nothing.
		void Begin(ConsoleAnchor at);

		// Moves the dragged end, leaving the pressed end where it was.
		void ExtendTo(ConsoleAnchor at);

		// Spans from the start of the first entry to the end of the last.
		void SelectAll(const std::vector<Core::LogEntry>& entries);

		void Clear();

		bool IsEmpty() const;

		// Where the selection crosses the message with this sequence number, given how many
		// columns that message has. Nothing when the selection does not reach it.
		std::optional<ConsoleColumnSpan> ColumnsOn(std::uint64_t sequence,
												   std::size_t columnCount) const;

		// The selected text of whichever of `entries` the selection crosses, one line per
		// entry. Entries the selection names but that are not in the vector - dropped from the
		// buffer, or hidden by the filter - contribute nothing, so what comes out is what the
		// reader could see.
		std::string ExtractText(const std::vector<Core::LogEntry>& entries) const;

	private:
		ConsoleAnchor First() const;
		ConsoleAnchor Last() const;

		ConsoleAnchor m_Pressed;
		ConsoleAnchor m_Dragged;
	};
}
