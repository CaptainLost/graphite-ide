#pragma once

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace Core {

	class TextBuffer;
}

namespace Graphite {

	struct SelectionOccurrences
	{
		// Document byte ranges in ascending order, the selection's own among them.
		std::vector<Core::FindMatch> Matches;

		// Ascending, each line once however many occurrences it holds.
		std::vector<std::size_t> Lines;
	};

	// Empty unless the selection is exactly one whole word, which is then found wherever it stands
	// whole and in the same case.
	SelectionOccurrences FindSelectionOccurrences(const Core::TextBuffer& buffer, Core::OffsetRange selection);

	struct OccurrenceMemo
	{
		Core::OffsetRange Selection;
		std::size_t Revision = 0;
		bool Filled = false;
		SelectionOccurrences Found;
	};

	// Nothing when the word is found only where it is selected. The find bar's own matches win: while it
	// shows any, the occurrences are not shown over them.
	const SelectionOccurrences& OccurrencesToShow(OccurrenceMemo& memo, const Core::TextBuffer& buffer,
												  const Core::OpenItem& item, bool enabled);

	struct OccurrenceMark
	{
		float From = 0.0f;
		float To = 0.0f;
	};

	std::vector<OccurrenceMark> BuildOccurrenceOverview(const std::vector<std::size_t>& lines,
														const std::function<float(std::size_t)>& lineTop,
														float minFraction);

	// lineEnd(line) is where the line's last row ends on the track, so a mark does not run on over rows drawn between
	// its line and the next, a peek's or a lens's.
	std::vector<OccurrenceMark> BuildOccurrenceOverview(const std::vector<std::size_t>& lines,
														const std::function<float(std::size_t)>& lineTop,
														const std::function<float(std::size_t)>& lineEnd,
														float minFraction);
}
