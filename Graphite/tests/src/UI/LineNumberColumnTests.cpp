#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "UI/LineNumberColumn.h"

#include "Core/ImGui/UiScale.h"

namespace {
	constexpr float kCellWidth = 10.0f;
}

TEST_CASE("A line number column holds as many digits as the file's last line has, three at least")
{
	ImGuiFrames frames;

	frames.Next([] {
		CHECK(Graphite::MeasureLineNumberColumn(9, kCellWidth).Digits == 3);
		CHECK(Graphite::MeasureLineNumberColumn(999, kCellWidth).Digits == 3);
		CHECK(Graphite::MeasureLineNumberColumn(1000, kCellWidth).Digits == 4);
		CHECK(Graphite::MeasureLineNumberColumn(12345, kCellWidth).Digits == 5);
	});
}

TEST_CASE("A line number column is its digits in the font's cells with a gap on either side")
{
	ImGuiFrames frames;

	frames.Next([] {
		const Graphite::LineNumberColumn column = Graphite::MeasureLineNumberColumn(12345, kCellWidth);
		const float gap = Core::Scaled(8.0f);

		CHECK(column.Width() == doctest::Approx(5.0f * kCellWidth + gap * 2.0f));
	});
}

TEST_CASE("A line number stands right-aligned, a gap short of the column's right edge")
{
	ImGuiFrames frames;

	frames.Next([] {
		const Graphite::LineNumberColumn column = Graphite::MeasureLineNumberColumn(500, kCellWidth);
		const float left = 100.0f;
		const float right = left + column.Width();
		const float gap = Core::Scaled(8.0f);

		CHECK(column.NumberLeft(left, 7) == doctest::Approx(right - gap - kCellWidth));
		CHECK(column.NumberLeft(left, 123) == doctest::Approx(right - gap - 3.0f * kCellWidth));
	});
}
