#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"
#include "Core/Text/TextSelection.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// Where the console's rows sit on screen, in the units ImGui draws them with. The origin is
	// the top left of the first row's text.
	struct ConsoleLayout
	{
		float OriginX = 0.0f;
		float OriginY = 0.0f;
		float RowHeight = 0.0f;
		float CellWidth = 0.0f;
	};

	// The console's side of the shared selection (ADR 0503): a line is an entry's Sequence cast
	// to int64, never its row, and a column counts tab-expanded characters (Core::ColumnCount).

	// The place in `shown` that a point on screen falls on: the row it is over and the character
	// boundary nearest to it, which puts the edge of a sweep between two characters rather than
	// always before the one under the pointer.
	//
	// A point above the rows lands at the start of the first entry and one below them at the end
	// of the last, so dragging out of the panel keeps selecting rather than stopping. Nothing
	// comes back when there are no entries to land on.
	std::optional<Core::TextPoint> PointAt(const std::vector<Core::LogEntry>& shown,
										   const ConsoleLayout& layout, float x, float y);

	// As PointAt, but the column of the character the point is over rather than the nearest
	// boundary - the right half of a character still names that character - which is where a
	// double-click looks for its word (Core::SelectionSource::CharacterAt).
	std::optional<Core::TextPoint> CharacterAt(const std::vector<Core::LogEntry>& shown,
											   const ConsoleLayout& layout, float x, float y);

	// The selected text of whichever of `shown` the selection crosses, one line per entry.
	// Entries the selection names but that are not in the vector - dropped from the buffer, or
	// hidden by the filter - contribute nothing, so what comes out is what the reader could see.
	//
	// The columns of the two ends become byte offsets into the raw message (Core::ByteOfColumn)
	// before the lines are joined, so tabs come out as tabs and a character is never split.
	std::string SelectedConsoleText(const Core::TextSelection& selection,
									const std::vector<Core::LogEntry>& shown);

	// The word under `at`, as a span of columns. A point on an entry that is not shown selects
	// nothing.
	Core::ColumnSpan ConsoleWordAt(const std::vector<Core::LogEntry>& shown, Core::TextPoint at);
}
