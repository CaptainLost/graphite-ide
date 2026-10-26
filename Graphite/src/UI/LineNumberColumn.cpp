#include "UI/LineNumberColumn.h"

#include "Core/ImGui/UiScale.h"

#include <algorithm>

namespace Graphite {

	namespace {
		constexpr int kFewestDigits = 3;
		constexpr std::size_t kDecimalBase = 10;
		constexpr float kGap = 8.0f;
		constexpr float kGapSides = 2.0f;

		int DigitCountOf(std::size_t number)
		{
			int digits = 1;

			for (std::size_t remaining = number; remaining >= kDecimalBase; remaining /= kDecimalBase)
			{
				++digits;
			}

			return digits;
		}
	}

	float LineNumberColumn::Width() const
	{
		return static_cast<float>(Digits) * CellWidth + Gap * kGapSides;
	}

	float LineNumberColumn::NumberLeft(float columnLeft, std::size_t number) const
	{
		return columnLeft + Width() - Gap - static_cast<float>(DigitCountOf(number)) * CellWidth;
	}

	int LineNumberDigits(std::size_t lastLineNumber)
	{
		return std::max(kFewestDigits, DigitCountOf(lastLineNumber));
	}

	LineNumberColumn MeasureLineNumberColumn(std::size_t lastLineNumber, float cellWidth)
	{
		return LineNumberColumn{.Digits = LineNumberDigits(lastLineNumber),
								.CellWidth = cellWidth,
								.Gap = Core::Scaled(kGap)};
	}
}
