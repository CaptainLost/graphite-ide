#include "Git/History/CommitColumns.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr float kMaxGraphShareOfTable = 0.25f;
		constexpr float kAlwaysShownColumnPaddings = 4.0f;

		float MessageWidthWith(const CommitColumnsInput& input, float graphWidth, bool showsAuthor, bool showsDate)
		{
			float used = graphWidth + input.Chevron + input.Hash + input.CellPadding * kAlwaysShownColumnPaddings;

			if (showsAuthor)
			{
				used += input.Author + input.CellPadding;
			}

			if (showsDate)
			{
				used += input.Date + input.CellPadding;
			}

			return input.Available - used;
		}
	}

	CommitColumns LayoutCommitColumns(const CommitColumnsInput& input)
	{
		CommitColumns columns;
		columns.Graph = std::min(input.Graph, input.Available * kMaxGraphShareOfTable);

		constexpr bool kShown = true;
		constexpr bool kGivenUp = false;

		columns.Message = MessageWidthWith(input, columns.Graph, kShown, kShown);

		if (columns.Message < input.MinMessage)
		{
			columns.ShowAuthor = false;
			columns.Message = MessageWidthWith(input, columns.Graph, kGivenUp, kShown);
		}

		if (columns.Message < input.MinMessage)
		{
			columns.ShowDate = false;
			columns.Message = MessageWidthWith(input, columns.Graph, kGivenUp, kGivenUp);
		}

		return columns;
	}
}
