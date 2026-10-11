#pragma once

#include <string_view>

namespace Graphite {

	// U+2026 HORIZONTAL ELLIPSIS: ends text that was cut short.
	inline constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

	// U+00B7 MIDDLE DOT with one space either side: joins the parts of a one-line summary.
	inline constexpr std::string_view kMiddleDotSeparator = " \xC2\xB7 ";

	// U+2014 EM DASH with one space either side: sets a server's problem apart from its name.
	inline constexpr std::string_view kEmDashSeparator = " \xE2\x80\x94 ";

	// U+2212 MINUS SIGN: as wide as '+', so a column of signed counts lines up.
	inline constexpr std::string_view kMinusSign = "\xE2\x88\x92";

	// U+00D7 MULTIPLICATION SIGN: a count of repeats, as in "Read ×3".
	inline constexpr std::string_view kMultiplicationSign = "\xC3\x97";

	// U+2192 RIGHTWARDS ARROW: from one value to the next, or to a return type.
	inline constexpr std::string_view kRightwardsArrow = "\xE2\x86\x92";

	// U+203A SINGLE RIGHT-POINTING ANGLE QUOTATION MARK with one space either side: from a page to
	// the page inside it, as in "Language Servers › C#".
	inline constexpr std::string_view kBreadcrumbSeparator = " \xE2\x80\xBA ";

	// U+201C and U+201D: open and close a quotation.
	inline constexpr std::string_view kLeftDoubleQuotationMark = "\xE2\x80\x9C";
	inline constexpr std::string_view kRightDoubleQuotationMark = "\xE2\x80\x9D";
}
