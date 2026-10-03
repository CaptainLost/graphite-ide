#pragma once

namespace Graphite {

	// What the commit table is given to lay out, all in pixels.
	struct CommitColumnsInput
	{
		// The width of the table.
		float Available = 0.0f;

		// The widths the columns open at. The graph's is as wide as the history has lanes.
		float Graph = 0.0f;
		float Chevron = 0.0f;
		float Hash = 0.0f;
		float Author = 0.0f;
		float Date = 0.0f;

		// What each visible column adds around its content, both sides.
		float CellPadding = 0.0f;

		// The least width the message column should keep while a column can be given up for it.
		float MinMessage = 0.0f;
	};

	struct CommitColumns
	{
		float Graph = 0.0f;
		bool ShowAuthor = true;
		bool ShowDate = true;

		// The width the stretched message column ends up with. Negative when even the columns
		// that are always shown do not fit.
		float Message = 0.0f;
	};

	// The graph is capped to a quarter of the table, so a history with many lanes does not
	// leave the message nothing. Then, while the message is under its minimum, the author is
	// given up first and the date second: the hash and the arrow stay, since they are what a
	// row is clicked by and looked up by.
	CommitColumns LayoutCommitColumns(const CommitColumnsInput& input);
}
