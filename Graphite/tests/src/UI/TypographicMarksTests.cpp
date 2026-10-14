#include <doctest/doctest.h>

#include "UI/TypographicMarks.h"

#include <ostream>
#include <string_view>

using namespace Graphite;

TEST_CASE("Every typographic mark is the UTF-8 encoding of its code point")
{
	CHECK(kEllipsis == std::string_view{"\xE2\x80\xA6", 3});
	CHECK(kMiddleDotSeparator == std::string_view{"\x20\xC2\xB7\x20", 4});
	CHECK(kMinusSign == std::string_view{"\xE2\x88\x92", 3});
	CHECK(kMultiplicationSign == std::string_view{"\xC3\x97", 2});
	CHECK(kRightwardsArrow == std::string_view{"\xE2\x86\x92", 3});
	CHECK(kLeftDoubleQuotationMark == std::string_view{"\xE2\x80\x9C", 3});
	CHECK(kRightDoubleQuotationMark == std::string_view{"\xE2\x80\x9D", 3});

	CHECK(kEllipsis.data()[kEllipsis.size()] == '\0');
}
