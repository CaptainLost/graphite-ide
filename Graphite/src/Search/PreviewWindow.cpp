#include "Search/PreviewWindow.h"

#include <algorithm>

namespace Graphite {

	PreviewWindow WindowAround(std::size_t line, std::size_t lineCount, std::size_t span)
	{
		if (lineCount == 0 || span == 0)
		{
			return PreviewWindow{0, 0};
		}

		const std::size_t last = lineCount - 1;

		// Every subtraction below is on std::size_t, where a negative result is an enormous
		// positive one. Each is guarded by the comparison above it rather than by clamping
		// afterwards, which would clamp a number that had already wrapped.
		if (lineCount <= span)
		{
			return PreviewWindow{0, last};
		}

		const std::size_t centred = line > last ? last : line;
		const std::size_t above = span / 2;

		std::size_t first = centred > above ? centred - above : 0;

		// The span fits — lineCount > span — so `last - (span - 1)` cannot wrap.
		const std::size_t latest = last - (span - 1);

		if (first > latest)
		{
			first = latest;
		}

		return PreviewWindow{first, first + span - 1};
	}

	float ScrollCentring(const ScrollCentringInput& input)
	{
		const float rowMiddle = input.ContentTop +
								static_cast<float>(input.RowsAbove) * input.LineHeight +
								input.LineHeight * 0.5f;

		return std::max(0.0f, rowMiddle - input.ViewHeight * 0.5f);
	}
}
