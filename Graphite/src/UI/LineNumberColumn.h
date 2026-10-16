#pragma once

#include <cstddef>

namespace Graphite {

	struct LineNumberColumn
	{
		int Digits = 0;
		float CellWidth = 0.0f;
		float Gap = 0.0f;

		float Width() const;

		float NumberLeft(float columnLeft, std::size_t number) const;
	};

	// WARNING: The cell width is the baked font's, not CalcTextSize's, which rounds each advance up and would set the
	// numbers off the text's columns.
	LineNumberColumn MeasureLineNumberColumn(std::size_t lastLineNumber, float cellWidth);
}
