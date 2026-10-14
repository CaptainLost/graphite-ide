#include "Search/PreviewWindow.h"

#include <algorithm>

namespace Graphite {

	PreviewWindow WindowAround(std::size_t line, std::size_t lineCount, std::size_t span)
	{
		if (lineCount == 0 || span == 0)
		{
			return PreviewWindow{.First = 0, .Last = 0};
		}

		const std::size_t lastLine = lineCount - 1;

		if (lineCount <= span)
		{
			return PreviewWindow{.First = 0, .Last = lastLine};
		}

		const std::size_t centredLine = line > lastLine ? lastLine : line;
		const std::size_t linesAbove = span / 2;

		std::size_t firstLine = centredLine > linesAbove ? centredLine - linesAbove : 0;

		// WARNING: These subtractions are on std::size_t, so each is guarded by the comparison
		// before it; clamping afterwards would clamp a number that had already wrapped.
		const std::size_t latestFirstLine = lastLine - (span - 1);

		if (firstLine > latestFirstLine)
		{
			firstLine = latestFirstLine;
		}

		return PreviewWindow{.First = firstLine, .Last = firstLine + span - 1};
	}

	float ScrollCentring(const ScrollCentringInput& input)
	{
		const float rowsAboveHeight = static_cast<float>(input.RowsAbove) * input.LineHeight;
		const float rowMiddle = input.ContentTop + rowsAboveHeight + input.LineHeight * 0.5f;
		const float viewMiddle = input.ViewHeight * 0.5f;

		return std::max(0.0f, rowMiddle - viewMiddle);
	}
}
