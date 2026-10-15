#include <doctest/doctest.h>

#include "Core/Base/Ascii.h"

#include <ostream>
#include <string>
#include <string_view>

TEST_CASE("LowerAscii lowers the ASCII capitals and leaves every other byte alone")
{
	CHECK(Core::LowerAscii('A') == 'a');
	CHECK(Core::LowerAscii('Z') == 'z');
	CHECK(Core::LowerAscii('a') == 'a');
	CHECK(Core::LowerAscii('.') == '.');
	CHECK(Core::LowerAscii('\xc4') == '\xc4');
}

TEST_CASE("EqualsIgnoringCase compares ASCII without regard to case, and nothing else")
{
	CHECK(Core::EqualsIgnoringCase("Content-Length", "content-length"));
	CHECK_FALSE(Core::EqualsIgnoringCase("abc", "abcd"));
	CHECK_FALSE(Core::EqualsIgnoringCase("bin", "bin "));
	CHECK_FALSE(Core::EqualsIgnoringCase("\xc4\x86", "\xc4\x87"));
	CHECK(Core::EqualsIgnoringCase("", ""));
}

TEST_CASE("Lowered lowers the ASCII capitals and keeps every other byte and the length")
{
	CHECK(Core::Lowered("Hello, WORLD") == "hello, world");
	CHECK(Core::Lowered("\xC3\x80Z\xC4") == "\xC3\x80z\xC4");
	CHECK(Core::Lowered(std::string_view{"A\0B", 3}) == std::string{"a\0b", 3});
	CHECK(Core::Lowered("").empty());
}

TEST_CASE("Two characters are equal ignoring ASCII case, and only ASCII case")
{
	static_assert(Core::EqualsIgnoringCase('a', 'A'));
	static_assert(Core::EqualsIgnoringCase('\0', '\0'));
	static_assert(!Core::EqualsIgnoringCase('[', '{'));
	static_assert(!Core::EqualsIgnoringCase('\xC4', '\xE4'));
	CHECK(Core::EqualsIgnoringCase('Z', 'z'));
}

TEST_CASE("IsAsciiWhitespace accepts exactly the six ASCII space bytes")
{
	constexpr int kByteValueCount = 256;
	constexpr int kSpaceByte = 0x20;
	constexpr int kTabByte = 0x09;
	constexpr int kCarriageReturnByte = 0x0D;

	for (int byteValue = 0; byteValue < kByteValueCount; ++byteValue)
	{
		const bool isSpace = byteValue == kSpaceByte;
		const bool isTabThroughCarriageReturn = byteValue >= kTabByte && byteValue <= kCarriageReturnByte;
		const bool expected = isSpace || isTabThroughCarriageReturn;

		CHECK(Core::IsAsciiWhitespace(static_cast<char>(byteValue)) == expected);
	}

	const char noBreakSpaceInWindows1252 = '\xA0';
	const char nextLineInLatin1 = '\x85';
	CHECK_FALSE(Core::IsAsciiWhitespace(noBreakSpaceInWindows1252));
	CHECK_FALSE(Core::IsAsciiWhitespace(nextLineInLatin1));
}

TEST_CASE("HexDigitValue reads 0-9, a-f and A-F and nothing else")
{
	static_assert(Core::HexDigitValue('0') == 0);
	static_assert(Core::HexDigitValue('9') == 9);
	static_assert(Core::HexDigitValue('a') == 10);
	static_assert(Core::HexDigitValue('F') == 15);
	CHECK_FALSE(Core::HexDigitValue('g').has_value());
	CHECK_FALSE(Core::HexDigitValue('G').has_value());
	CHECK_FALSE(Core::HexDigitValue('\xB0').has_value());
	CHECK_FALSE(Core::HexDigitValue('\0').has_value());
}

TEST_CASE("The lowercase hex digits are sixteen, in order")
{
	static_assert(Core::kLowercaseHexDigits.size() == 16);
	CHECK(Core::kLowercaseHexDigits[10] == 'a');
	CHECK(Core::kLowercaseHexDigits[15] == 'f');
}

TEST_CASE("LoweredTextContains finds a lowered needle in text of any case and never an upper-case needle")
{
	CHECK(Core::LoweredTextContains("Hello World", "world"));
	CHECK_FALSE(Core::LoweredTextContains("Hello World", "World"));
	CHECK(Core::LoweredTextContains("abc", ""));
	CHECK(Core::LoweredTextContains("", ""));
	CHECK_FALSE(Core::LoweredTextContains("\xC4\x84", "\xC4\x85"));
}

TEST_CASE("The trims drop bytes of the given set from one end or both and keep a view into the text")
{
	const std::string_view padded = " \tab \r";

	CHECK(Core::WithoutLeading(padded, " \t") == "ab \r");
	CHECK(Core::WithoutTrailing(padded, " \t\r") == " \tab");
	CHECK(Core::WithoutTrailing(padded, " \t\r").data() == padded.data());
	CHECK(Core::WithoutSurrounding("  a b  ", " ") == "a b");
	CHECK(Core::WithoutSurrounding("\tx\t", " ") == "\tx\t");

	const std::string_view blank = "   ";
	CHECK(Core::WithoutLeading(blank, " ").empty());
	CHECK(Core::WithoutLeading(blank, " ").data() == blank.data() + blank.size());
	CHECK(Core::WithoutTrailing(blank, " ").data() == blank.data());
	CHECK(Core::WithoutSurrounding("", " ").empty());
}

TEST_CASE("IsAsciiDigit is true for the ten ASCII digits alone")
{
	static_assert(Core::IsAsciiDigit('0'));
	static_assert(Core::IsAsciiDigit('9'));
	static_assert(!Core::IsAsciiDigit('a'));
	static_assert(!Core::IsAsciiDigit('/'));
	static_assert(!Core::IsAsciiDigit(':'));
	CHECK_FALSE(Core::IsAsciiDigit('\xD9'));
}

TEST_CASE("ContainsOnlySpacesAndTabs is true for spaces and tabs only, and for nothing at all")
{
	static_assert(Core::ContainsOnlySpacesAndTabs(""));
	static_assert(Core::ContainsOnlySpacesAndTabs(" \t "));
	static_assert(!Core::ContainsOnlySpacesAndTabs(" \r"));
	static_assert(!Core::ContainsOnlySpacesAndTabs("\xA0"));
	CHECK_FALSE(Core::ContainsOnlySpacesAndTabs(std::string_view{"\0", 1}));
}
