#include <doctest/doctest.h>

#include "Core/Text/Codec.h"

#include <string>

using namespace Core;

namespace {
	// A string holding a NUL is built with an explicit length: built from the literal alone it
	// would stop at the first NUL.
	const std::string kHiLittleEndian = std::string("H\0i\0", 4);
	const std::string kHiBigEndian = std::string("\0H\0i", 4);
}

TEST_CASE("UTF-8 passes through decoding untouched")
{
	const Result<std::string> decoded = Decode("plain text", Encoding::Utf8);

	REQUIRE(decoded);
	CHECK(decoded.GetValue() == "plain text");
}

TEST_CASE("Unknown passes through decoding untouched")
{
	const std::string polishWordInWindows1250 = "cz\xEA\x9C\xE6";
	const Result<std::string> decoded = Decode(polishWordInWindows1250, Encoding::Unknown);

	REQUIRE(decoded);
	CHECK(decoded.GetValue() == polishWordInWindows1250);
}

TEST_CASE("UTF-16 decodes in both byte orders")
{
	const Result<std::string> fromLittleEndian = Decode(kHiLittleEndian, Encoding::Utf16Le);
	const Result<std::string> fromBigEndian = Decode(kHiBigEndian, Encoding::Utf16Be);

	REQUIRE(fromLittleEndian);
	REQUIRE(fromBigEndian);
	CHECK(fromLittleEndian.GetValue() == "Hi");
	CHECK(fromBigEndian.GetValue() == "Hi");
}

TEST_CASE("An empty file decodes to nothing rather than failing")
{
	const Result<std::string> decoded = Decode("", Encoding::Utf16Le);

	REQUIRE(decoded);
	CHECK(decoded.GetValue() == "");
}

TEST_CASE("Characters outside the basic plane decode from a surrogate pair")
{
	const std::string slightlySmilingFaceInUtf16LittleEndian = "\x3D\xD8\x42\xDE";
	const Result<std::string> decoded = Decode(slightlySmilingFaceInUtf16LittleEndian, Encoding::Utf16Le);

	REQUIRE(decoded);
	CHECK(decoded.GetValue() == "\xF0\x9F\x99\x82");
}

TEST_CASE("Polish characters survive the round trip in both byte orders")
{
	const std::string polishPhrase = "zaz\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85";

	for (const Encoding encoding : {Encoding::Utf16Le, Encoding::Utf16Be})
	{
		const std::string encoded = Encode(polishPhrase, encoding);
		const Result<std::string> decoded = Decode(encoded, encoding);

		REQUIRE(decoded);
		CHECK(decoded.GetValue() == polishPhrase);
	}
}

TEST_CASE("An emoji survives the round trip, which is the only case that makes a pair")
{
	const std::string emojiBetweenLetters = "a\xF0\x9F\x99\x82z";

	for (const Encoding encoding : {Encoding::Utf16Le, Encoding::Utf16Be})
	{
		const Result<std::string> decoded = Decode(Encode(emojiBetweenLetters, encoding), encoding);

		REQUIRE(decoded);
		CHECK(decoded.GetValue() == emojiBetweenLetters);
	}
}

TEST_CASE("Bytes decoded and re-encoded come back identical")
{
	const Result<std::string> decoded = Decode(kHiLittleEndian, Encoding::Utf16Le);

	REQUIRE(decoded);
	CHECK(Encode(decoded.GetValue(), Encoding::Utf16Le) == kHiLittleEndian);
}

TEST_CASE("An odd number of bytes is a truncated file, not a character")
{
	const Result<std::string> decoded = Decode(std::string("H\0i", 3), Encoding::Utf16Le);

	REQUIRE_FALSE(decoded);
	CHECK(decoded.GetError().Message.find("odd") != std::string::npos);
}

TEST_CASE("A high surrogate without a low one fails")
{
	const std::string highSurrogateThenOrdinaryCharacter = std::string("\x3D\xD8\x69\x00", 4);
	const Result<std::string> decoded = Decode(highSurrogateThenOrdinaryCharacter, Encoding::Utf16Le);

	REQUIRE_FALSE(decoded);
	CHECK(decoded.GetError().Message.find("surrogate") != std::string::npos);
}

TEST_CASE("A high surrogate at the very end fails")
{
	const std::string loneHighSurrogate = "\x3D\xD8";
	const Result<std::string> decoded = Decode(loneHighSurrogate, Encoding::Utf16Le);

	REQUIRE_FALSE(decoded);
	CHECK(decoded.GetError().Message.find("surrogate") != std::string::npos);
}

TEST_CASE("A low surrogate with nothing before it fails")
{
	const std::string loneLowSurrogate = "\x42\xDE";
	const Result<std::string> decoded = Decode(loneLowSurrogate, Encoding::Utf16Le);

	REQUIRE_FALSE(decoded);
	CHECK(decoded.GetError().Message.find("surrogate") != std::string::npos);
}

TEST_CASE("The error names where it happened, so a damaged file can be looked at")
{
	const std::string hiThenStrayLowSurrogate = std::string("H\0i\0\x42\xDE", 6);
	const std::string offsetOfStrayLowSurrogate = "4";
	const Result<std::string> decoded = Decode(hiThenStrayLowSurrogate, Encoding::Utf16Le);

	REQUIRE_FALSE(decoded);
	CHECK(decoded.GetError().Message.find(offsetOfStrayLowSurrogate) != std::string::npos);
}

TEST_CASE("Encoding passes UTF-8, its BOM variant and Unknown through unchanged so a read-only file that failed validation is written back byte-identical")
{
	const std::string polishWordInWindows1250 = "cz\xEA\x9C\xE6";

	CHECK(Encode(polishWordInWindows1250, Encoding::Utf8) == polishWordInWindows1250);
	CHECK(Encode(polishWordInWindows1250, Encoding::Utf8Bom) == polishWordInWindows1250);
	CHECK(Encode(polishWordInWindows1250, Encoding::Unknown) == polishWordInWindows1250);
}

TEST_CASE("Encoding writes the byte order it was asked for")
{
	CHECK(Encode("Hi", Encoding::Utf16Le) == kHiLittleEndian);
	CHECK(Encode("Hi", Encoding::Utf16Be) == kHiBigEndian);
}

TEST_CASE("Encoding to UTF-16 writes one replacement for each broken sequence rather than assembling its bits")
{
	const std::string strayContinuationByteBetweenCharacters = "a\x80!";
	const std::string byteThatCannotLeadThenThreeContinuationBytes = "\xF5\x80\x80\x80";
	const std::string truncatedThreeByteSequenceThenLetter = "\xE2\x82"
															 "a";
	const std::string replacementCharacterLittleEndian = std::string("\xFD\xFF", 2);

	CHECK(Encode(strayContinuationByteBetweenCharacters, Encoding::Utf16Le) == std::string("a\0", 2) + replacementCharacterLittleEndian + std::string("!\0", 2));
	CHECK(Encode(byteThatCannotLeadThenThreeContinuationBytes, Encoding::Utf16Le) == replacementCharacterLittleEndian + replacementCharacterLittleEndian + replacementCharacterLittleEndian + replacementCharacterLittleEndian);
	CHECK(Encode(truncatedThreeByteSequenceThenLetter, Encoding::Utf16Le) == replacementCharacterLittleEndian + std::string("a\0", 2));
}

TEST_CASE("A byte order mark settles the encoding on its own")
{
	CHECK(SniffEncoding("\xEF\xBB\xBFhello") == Encoding::Utf8Bom);
	CHECK(SniffEncoding(std::string("\xFF\xFEH\0i\0", 6)) == Encoding::Utf16Le);
	CHECK(SniffEncoding(std::string("\xFE\xFF\0H\0i", 6)) == Encoding::Utf16Be);
}

TEST_CASE("Ordinary text with no mark is UTF-8")
{
	CHECK(SniffEncoding("int main() {}") == Encoding::Utf8);
	CHECK(SniffEncoding("") == Encoding::Utf8);
}

TEST_CASE("UTF-16 without a mark is recognised from the parity of its NUL bytes")
{
	const std::string helloWithNulsAtOddOffsets = std::string("H\0e\0l\0l\0o\0", 10);
	const std::string helloWithNulsAtEvenOffsets = std::string("\0H\0e\0l\0l\0o", 10);

	CHECK(SniffEncoding(helloWithNulsAtOddOffsets) == Encoding::Utf16Le);
	CHECK(SniffEncoding(helloWithNulsAtEvenOffsets) == Encoding::Utf16Be);
}

TEST_CASE("A file with NUL bytes on both parities is not UTF-16")
{
	const std::string nulsOnBothParities = std::string("\0\0a\0\0b\0\0", 8);

	CHECK(SniffEncoding(nulsOnBothParities) == Encoding::Utf8);
}

TEST_CASE("A single NUL byte is not enough evidence")
{
	CHECK(SniffEncoding(std::string("ab\0d", 4)) == Encoding::Utf8);
}

TEST_CASE("Binary data whose NULs happen to line up is caught by the decode check")
{
	const std::string controlCharactersWithNulsAtOddOffsets = std::string("\x01\0\x02\0\x03\0\x04\0", 8);

	CHECK(SniffEncoding(controlCharactersWithNulsAtOddOffsets) == Encoding::Utf8);
}

TEST_CASE("Text containing tabs and newlines is still text")
{
	const std::string tabAndNewlineBetweenLetters = std::string("a\0\t\0\n\0b\0", 8);

	CHECK(SniffEncoding(tabAndNewlineBetweenLetters) == Encoding::Utf16Le);
}

TEST_CASE("An unpaired surrogate means the bytes are not UTF-16 text")
{
	const std::string highSurrogateThenOrdinaryCharacters = std::string("\x3D\xD8\x69\0\x69\0", 6);

	CHECK(SniffEncoding(highSurrogateThenOrdinaryCharacters) == Encoding::Utf8);
}

TEST_CASE("A sample cut mid-unit is still recognised")
{
	const std::string fourUnitsThenOneStrayByte = std::string("H\0e\0l\0l\0o", 9);

	CHECK(SniffEncoding(fourUnitsThenOneStrayByte) == Encoding::Utf16Le);
}

TEST_CASE("A character outside Latin-1 does not break the parity rule while nine tenths of the NULs share one parity")
{
	const std::string nineAsciiLettersThenCapitalAWithMacron = std::string("a\0b\0c\0d\0e\0f\0g\0h\0i\0\0\x01", 20);
	const std::string eightAsciiLettersThenCapitalAWithMacron = std::string("b\0c\0d\0e\0f\0g\0h\0i\0\0\x01", 18);

	CHECK(SniffEncoding(nineAsciiLettersThenCapitalAWithMacron) == Encoding::Utf16Le);
	CHECK(SniffEncoding(eightAsciiLettersThenCapitalAWithMacron) == Encoding::Utf8);
}

TEST_CASE("Decoded C1 control characters are not text")
{
	const std::string c1ControlCharactersWithNulsAtOddOffsets = std::string("\x85\0\x86\0\x87\0\x88\0", 8);

	CHECK(SniffEncoding(c1ControlCharactersWithNulsAtOddOffsets) == Encoding::Utf8);
}

TEST_CASE("Decoded DEL is not text")
{
	const std::string deleteBeforeEachOfTwoLetters = std::string("\x7F\0\x41\0\x7F\0\x42\0", 8);

	CHECK(SniffEncoding(deleteBeforeEachOfTwoLetters) == Encoding::Utf8);
}

TEST_CASE("DecodeWithoutByteOrderMark strips the mark the bytes carry, whatever the encoding says")
{
	CHECK(DecodeWithoutByteOrderMark("\xEF\xBB\xBF"
									 "abc",
									 Encoding::Utf8Bom)
			  .GetValue() == "abc");
	CHECK(DecodeWithoutByteOrderMark("abc", Encoding::Utf8Bom).GetValue() == "abc");
	CHECK(DecodeWithoutByteOrderMark("\xFF\xFE"
									 "abc",
									 Encoding::Utf8)
			  .GetValue() == "abc");
	CHECK(DecodeWithoutByteOrderMark(std::string{"\xFF\xFEH\0i\0", 6}, Encoding::Utf16Le).GetValue() == "Hi");
	CHECK(DecodeWithoutByteOrderMark(std::string{"H\0i\0", 4}, Encoding::Utf16Le).GetValue() == "Hi");
	CHECK(DecodeWithoutByteOrderMark("", Encoding::Utf8).GetValue().empty());
	CHECK_FALSE(DecodeWithoutByteOrderMark("\xFF\xFEH", Encoding::Utf16Le).HasValue());
}
