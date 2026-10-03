#include "Git/History/CommitColumns.h"

#include <algorithm>

namespace Graphite {

	CommitColumns LayoutCommitColumns(const CommitColumnsInput& input)
	{
		CommitColumns columns;
		columns.Graph = std::min(input.Graph, input.Available * 0.25f);

		// The graph, arrow, hash and message always have their padding; ImGui spends exactly
		// two cell paddings on each enabled column and nothing more with the flags used.
		const auto messageWith = [&](bool author, bool date) {
			float used = columns.Graph + input.Chevron + input.Hash + input.CellPadding * 4.0f;

			if (author)
			{
				used += input.Author + input.CellPadding;
			}

			if (date)
			{
				used += input.Date + input.CellPadding;
			}

			return input.Available - used;
		};

		columns.Message = messageWith(true, true);

		if (columns.Message < input.MinMessage)
		{
			columns.ShowAuthor = false;
			columns.Message = messageWith(false, true);
		}

		if (columns.Message < input.MinMessage)
		{
			columns.ShowDate = false;
			columns.Message = messageWith(false, false);
		}

		return columns;
	}
}
