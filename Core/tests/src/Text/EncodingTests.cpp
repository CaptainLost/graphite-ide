#include <doctest/doctest.h>

#include "Core/Text/Encoding.h"

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

using namespace Core;

namespace {
	constexpr int kByteValueCount = 256;
}

TEST_CASE("A byte order mark names the encoding and no mark is read as UTF-8")
{
	CHECK(DetectEncoding("\xEF\xBB\xBFhello") == Encoding::Utf8Bom);
	CHECK(DetectEncoding(std::string_view{"\xFF\xFEh\0", 4}) == Encoding::Utf16Le);
	CHECK(DetectEncoding(std::string_view{"\xFE\xFF\0h", 4}) == Encoding::Utf16Be);

	CHECK(DetectEncoding("hello") == Encoding::Utf8);
	CHECK(DetectEncoding("") == Encoding::Utf8);
}

TEST_CASE("A file shorter than the mark it would need is not mistaken for one")
{
	CHECK(DetectEncoding("\xEF") == Encoding::Utf8);
	CHECK(DetectEncoding("\xEF\xBB") == Encoding::Utf8);
	CHECK(DetectEncoding("\xFF") == Encoding::Utf8);
}

TEST_CASE("The mark's length is what has to be stripped")
{
	CHECK(LeadingByteOrderMarkLength("\xEF\xBB\xBFhello") == 3);
	CHECK(LeadingByteOrderMarkLength("\xFF\xFEh") == 2);
	CHECK(LeadingByteOrderMarkLength("\xFE\xFFh") == 2);
	CHECK(LeadingByteOrderMarkLength("hello") == 0);
	CHECK(LeadingByteOrderMarkLength("") == 0);
}

TEST_CASE("Well-formed UTF-8 validates")
{
	const std::string_view polishCzesc = "cz\xC4\x99\xC5\x9B\xC4\x87";
	const std::string_view cyrillicPri = "\xD0\x9F\xD1\x80\xD0\xB8";
	const std::string_view japaneseNihon = "\xE6\x97\xA5\xE6\x9C\xAC";
	const std::string_view slightlySmilingFaceEmoji = "\xF0\x9F\x99\x82";

	CHECK(IsValidUtf8(""));
	CHECK(IsValidUtf8("plain ascii"));
	CHECK(IsValidUtf8(polishCzesc));
	CHECK(IsValidUtf8(cyrillicPri));
	CHECK(IsValidUtf8(japaneseNihon));
	CHECK(IsValidUtf8(slightlySmilingFaceEmoji));
}

TEST_CASE("Malformed UTF-8 is rejected in each of the ways it can be malformed")
{
	const std::string_view twoByteSequenceCutOffByTheEnd = "abc\xC4";
	const std::string_view fourByteSequenceCutOffByTheEnd = "abc\xF0\x9F\x99";
	const std::string_view continuationByteWithNoLeadByte = "\x99 abc";
	const std::string_view leadByteFollowedByNonContinuationByte = "\xC4Z";
	const std::string_view slashOverlongInTwoBytes = "\xC0\xAF";
	const std::string_view nulOverlongInTwoBytes = "\xC0\x80";
	const std::string_view encodedSurrogateHalf = "\xED\xA0\x80";
	const std::string_view codePointAbove10FFFF = "\xF5\x80\x80\x80";
	const std::string_view fiveByteLeadByte = "\xF8\x80\x80\x80\x80";

	CHECK_FALSE(IsValidUtf8(twoByteSequenceCutOffByTheEnd));
	CHECK_FALSE(IsValidUtf8(fourByteSequenceCutOffByTheEnd));
	CHECK_FALSE(IsValidUtf8(continuationByteWithNoLeadByte));
	CHECK_FALSE(IsValidUtf8(leadByteFollowedByNonContinuationByte));
	CHECK_FALSE(IsValidUtf8(slashOverlongInTwoBytes));
	CHECK_FALSE(IsValidUtf8(nulOverlongInTwoBytes));
	CHECK_FALSE(IsValidUtf8(encodedSurrogateHalf));
	CHECK_FALSE(IsValidUtf8(codePointAbove10FFFF));
	CHECK_FALSE(IsValidUtf8(fiveByteLeadByte));
}

TEST_CASE("A Windows-1250 file is not valid UTF-8 so it stays read-only rather than turning mixed on the first keystroke")
{
	const std::string_view polishCzescInWindows1250 = "cz\xEA\x9C\xE6";

	CHECK_FALSE(IsValidUtf8(polishCzescInWindows1250));
}

TEST_CASE("Stepping forward crosses a whole character")
{
	const std::string oneTwoFourAndOneByteCharacters = "a\xC4\x99\xF0\x9F\x99\x82z";

	CHECK(NextCharacter(oneTwoFourAndOneByteCharacters, 0) == 1);
	CHECK(NextCharacter(oneTwoFourAndOneByteCharacters, 1) == 3);
	CHECK(NextCharacter(oneTwoFourAndOneByteCharacters, 3) == 7);
	CHECK(NextCharacter(oneTwoFourAndOneByteCharacters, 7) == 8);
}

TEST_CASE("Stepping backward crosses a whole character")
{
	const std::string oneTwoFourAndOneByteCharacters = "a\xC4\x99\xF0\x9F\x99\x82z";

	CHECK(PreviousCharacter(oneTwoFourAndOneByteCharacters, 8) == 7);
	CHECK(PreviousCharacter(oneTwoFourAndOneByteCharacters, 7) == 3);
	CHECK(PreviousCharacter(oneTwoFourAndOneByteCharacters, 3) == 1);
	CHECK(PreviousCharacter(oneTwoFourAndOneByteCharacters, 1) == 0);
}

TEST_CASE("Stepping stops at the ends instead of running off them")
{
	const std::string twoAsciiLetters = "ab";

	CHECK(NextCharacter(twoAsciiLetters, 2) == 2);
	CHECK(NextCharacter(twoAsciiLetters, 99) == 2);
	CHECK(PreviousCharacter(twoAsciiLetters, 0) == 0);
	CHECK(PreviousCharacter("", 0) == 0);
}

TEST_CASE("Stepping out of the middle of a character lands on a boundary and not on another interior byte even though no caller should ask for that")
{
	const std::string fourByteCharacter = "\xF0\x9F\x99\x82";

	CHECK(PreviousCharacter(fourByteCharacter, 2) == 0);
	CHECK(NextCharacter(fourByteCharacter, 1) == 4);
}

TEST_CASE("Every encoding has a name to show")
{
	CHECK(EncodingName(Encoding::Utf8) == "UTF-8");
	CHECK(EncodingName(Encoding::Utf8Bom) == "UTF-8 with BOM");
	CHECK(EncodingName(Encoding::Utf16Le) == "UTF-16 LE");
	CHECK(EncodingName(Encoding::Utf16Be) == "UTF-16 BE");
	CHECK(EncodingName(Encoding::Unknown) == "Unknown");
}

TEST_CASE("Each encoding knows the mark it is written with")
{
	CHECK(ByteOrderMarkBytes(Encoding::Utf8Bom) == std::string_view{"\xEF\xBB\xBF", 3});
	CHECK(ByteOrderMarkBytes(Encoding::Utf16Le) == std::string_view{"\xFF\xFE", 2});
	CHECK(ByteOrderMarkBytes(Encoding::Utf16Be) == std::string_view{"\xFE\xFF", 2});
	CHECK(ByteOrderMarkBytes(Encoding::Utf8).empty());
	CHECK(ByteOrderMarkBytes(Encoding::Unknown).empty());
}

TEST_CASE("The mark read from a file's leading bytes is as long as the mark written so a file neither gains nor loses bytes through an open and a save")
{
	for (const Encoding encoding :
		 {Encoding::Utf8, Encoding::Utf8Bom, Encoding::Utf16Le, Encoding::Utf16Be})
	{
		const std::string_view mark = ByteOrderMarkBytes(encoding);

		CHECK(LeadingByteOrderMarkLength(std::string{mark} + "rest") == mark.size());
	}
}

TEST_CASE("A continuation byte is exactly one of 0x80 to 0xBF, read without sign extension")
{
	constexpr int kLowestContinuationByte = 0x80;
	constexpr int kHighestContinuationByte = 0xBF;

	for (int byteValue = 0; byteValue < kByteValueCount; ++byteValue)
	{
		const bool expectedIsContinuation = byteValue >= kLowestContinuationByte && byteValue <= kHighestContinuationByte;

		CHECK(IsUtf8ContinuationByte(static_cast<char>(byteValue)) == expectedIsContinuation);
	}

	static_assert(IsUtf8ContinuationByte('\x80'));
	static_assert(IsUtf8ContinuationByte('\xBF'));
	static_assert(!IsUtf8ContinuationByte('\xC0'));
	static_assert(!IsUtf8ContinuationByte('\0'));
}

TEST_CASE("A lead byte's claimed length is taken at its word even for overlong leads and F5 to F7 unlike DecodeUtf8 and any byte that cannot lead counts as one")
{
	constexpr int kFirstByteClaimingTwoBytes = 0xC0;
	constexpr int kLastByteClaimingTwoBytes = 0xDF;
	constexpr int kFirstByteClaimingThreeBytes = 0xE0;
	constexpr int kLastByteClaimingThreeBytes = 0xEF;
	constexpr int kFirstByteClaimingFourBytes = 0xF0;
	constexpr int kLastByteClaimingFourBytes = 0xF7;

	for (int byteValue = 0; byteValue < kByteValueCount; ++byteValue)
	{
		std::size_t expectedLength = 1;

		if (byteValue >= kFirstByteClaimingTwoBytes && byteValue <= kLastByteClaimingTwoBytes)
		{
			expectedLength = 2;
		}
		else if (byteValue >= kFirstByteClaimingThreeBytes && byteValue <= kLastByteClaimingThreeBytes)
		{
			expectedLength = 3;
		}
		else if (byteValue >= kFirstByteClaimingFourBytes && byteValue <= kLastByteClaimingFourBytes)
		{
			expectedLength = 4;
		}

		CHECK(Utf8SequenceLength(static_cast<char>(byteValue)) == expectedLength);
	}

	const char overlongTwoByteLead = '\xC0';
	const char leadAboveLastCodePoint = '\xF5';
	const char continuationByte = '\x80';
	const char byteThatNeverLeads = '\xFF';

	CHECK(Utf8SequenceLength(overlongTwoByteLead) == 2);
	CHECK(Utf8SequenceLength(leadAboveLastCodePoint) == 4);
	CHECK(Utf8SequenceLength(continuationByte) == 1);
	CHECK(Utf8SequenceLength(byteThatNeverLeads) == 1);
}

TEST_CASE("CharacterBoundaryAtOrBefore backs off to a character's first byte and clamps to the end")
{
	const std::string_view asciiThenTwoByteCharacter = "a\xC4\x99";

	CHECK(CharacterBoundaryAtOrBefore(asciiThenTwoByteCharacter, 0) == 0);
	CHECK(CharacterBoundaryAtOrBefore(asciiThenTwoByteCharacter, 1) == 1);
	CHECK(CharacterBoundaryAtOrBefore(asciiThenTwoByteCharacter, 2) == 1);
	CHECK(CharacterBoundaryAtOrBefore(asciiThenTwoByteCharacter, 3) == 3);
	CHECK(CharacterBoundaryAtOrBefore(asciiThenTwoByteCharacter, 99) == 3);

	CHECK(CharacterBoundaryAtOrBefore("", 0) == 0);
	CHECK(CharacterBoundaryAtOrBefore("", 5) == 0);

	const std::string_view strayContinuationBytes = "\x80\x80";

	CHECK(CharacterBoundaryAtOrBefore(strayContinuationBytes, 1) == 0);
	CHECK(CharacterBoundaryAtOrBefore(strayContinuationBytes, 2) == 2);
}

TEST_CASE("CharacterBoundaryAtOrAfter skips continuation bytes and leaves an offset past the end alone")
{
	const std::string_view twoByteCharacterBetweenAsciiLetters = "a\xC4\x99"
																 "b";

	CHECK(CharacterBoundaryAtOrAfter(twoByteCharacterBetweenAsciiLetters, 1) == 1);
	CHECK(CharacterBoundaryAtOrAfter(twoByteCharacterBetweenAsciiLetters, 2) == 3);
	CHECK(CharacterBoundaryAtOrAfter(twoByteCharacterBetweenAsciiLetters, 4) == 4);
	CHECK(CharacterBoundaryAtOrAfter(twoByteCharacterBetweenAsciiLetters, 9) == 9);
	CHECK(CharacterBoundaryAtOrAfter("", 0) == 0);
	CHECK(CharacterBoundaryAtOrAfter("", 1) == 1);
	CHECK(CharacterBoundaryAtOrAfter("\x80\x80", 0) == 2);
}

TEST_CASE("CharacterBoundaries lists every offset a character starts at, and the end")
{
	using Boundaries = std::vector<std::size_t>;

	CHECK(CharacterBoundaries("") == Boundaries{0});
	CHECK(CharacterBoundaries("ab") == Boundaries{0, 1, 2});
	CHECK(CharacterBoundaries("a\xC4\x99"
							  "b") == Boundaries{0, 1, 3, 4});
}

TEST_CASE("CharacterBoundaries starts at 0 even when the text starts partway through a character")
{
	CHECK(CharacterBoundaries("\x99"
							  "b") == std::vector<std::size_t>{0, 1, 2});
}

TEST_CASE("CharacterCount counts the bytes that start a character")
{
	CHECK(CharacterCount("") == 0);
	CHECK(CharacterCount("abc") == 3);
	CHECK(CharacterCount("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87") == 6);
	CHECK(CharacterCount("\x80") == 0);
	CHECK(CharacterCount("\xC4") == 1);
	CHECK(CharacterCount("\xF8\xFF") == 2);
	CHECK(CharacterCount(std::string_view{"a\0b", 3}) == 3);
}

TEST_CASE("WithoutUtf8ByteOrderMark drops one UTF-8 mark and nothing else")
{
	CHECK(WithoutUtf8ByteOrderMark("\xEF\xBB\xBFx") == "x");
	CHECK(WithoutUtf8ByteOrderMark("\xEF\xBB\xBF\xEF\xBB\xBFx") == "\xEF\xBB\xBFx");
	CHECK(WithoutUtf8ByteOrderMark("\xEF\xBB") == "\xEF\xBB");
	CHECK(WithoutUtf8ByteOrderMark("\xFF\xFEx") == "\xFF\xFEx");
	CHECK(WithoutUtf8ByteOrderMark("").empty());
}
