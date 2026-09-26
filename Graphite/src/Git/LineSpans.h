#pragma once

#include <cstddef>
#include <string_view>

namespace Graphite {

	// A half-open byte range within one line.
	struct ChangedSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;

		friend bool operator==(const ChangedSpan&, const ChangedSpan&) = default;
	};

	// The part of `oldLine` that is gone from `newLine`: everything between the text the two
	// share at the start and the text they share at the end. Empty when the old line survives
	// whole, which is also what a pure insertion looks like.
	//
	// Deliberately not a character diff. Two edits far apart in one line cover everything
	// between them, which is the price of an answer that is cheap, stable and needs no tuning;
	// the common case, one edit per line, comes out exact.
	//
	// Both ends land on UTF-8 character boundaries, so the span never cuts a character in half.
	ChangedSpan RemovedSpan(std::string_view oldLine, std::string_view newLine);
}
