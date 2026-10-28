#pragma once

#include "Core/Base/Result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Core {

	// Zero-based. A side with no lines in the run still has a Start: the number of that side's
	// lines before the run, which is where a deletion is drawn between two lines.
	struct LineHunk
	{
		std::size_t OldStart = 0;
		std::size_t OldCount = 0;
		std::size_t NewStart = 0;
		std::size_t NewCount = 0;

		friend bool operator==(const LineHunk&, const LineHunk&) = default;
	};

	// Every byte is treated as text, a NUL included; deciding whether a file is text at all is
	// the caller's job.
	Result<std::vector<LineHunk>> DiffTexts(std::string_view oldText, std::string_view newText);

	enum class LineFate : std::uint8_t
	{
		Unchanged,
		Changed,
		Added,
	};

	struct TracedLine
	{
		LineFate Fate = LineFate::Unchanged;
		std::size_t OldLine = 0; // Unused when Added.
	};

	// Zero-based on both sides, hunks in ascending order. A line outside every change keeps its
	// identity, shifted by the changes above it. A line inside a change that replaced k old lines
	// with m new ones, at position i of the new side, is Changed from old start + i when i < k and
	// Added otherwise.
	TracedLine TraceLineToOld(std::span<const LineHunk> hunks, std::size_t newLine);
}
