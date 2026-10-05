#include <doctest/doctest.h>

#include "Core/Text/Encoding.h"

#include <ostream>
#include <string>

using namespace Core;

TEST_CASE("A byte order mark names the encoding")
{
	CHECK(DetectEncoding("\xEF\xBB\xBFhello") == Encoding::Utf8Bom);
	CHECK(DetectEncoding("\xFF\xFEh\0") == Encoding::Utf16Le);
	CHECK(DetectEncoding("\xFE\xFF\0h") == Encoding::Utf16Be);

	// No mark means no claim, which we read as UTF-8.
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
	CHECK(BomLength("\xEF\xBB\xBFhello") == 3);
	CHECK(BomLength("\xFF\xFEh") == 2);
	CHECK(BomLength("\xFE\xFFh") == 2);
	CHECK(BomLength("hello") == 0);
	CHECK(BomLength("") == 0);
}

TEST_CASE("Well-formed UTF-8 validates")
{
	CHECK(IsValidUtf8(""));
	CHECK(IsValidUtf8("plain ascii"));
	CHECK(IsValidUtf8("cz\xC4\x99\xC5\x9B\xC4\x87")); // częś in UTF-8
	CHECK(IsValidUtf8("\xD0\x9F\xD1\x80\xD0\xB8"));	  // При
	CHECK(IsValidUtf8("\xE6\x97\xA5\xE6\x9C\xAC"));	  // 日本
	CHECK(IsValidUtf8("\xF0\x9F\x99\x82"));			  // 🙂
}

TEST_CASE("Malformed UTF-8 is rejected in each of the ways it can be malformed")
{
	// A sequence cut off by the end of the file.
	CHECK_FALSE(IsValidUtf8("abc\xC4"));
	CHECK_FALSE(IsValidUtf8("abc\xF0\x9F\x99"));

	// A continuation byte with no lead byte in front of it.
	CHECK_FALSE(IsValidUtf8("\x99 abc"));

	// A lead byte followed by something that is not a continuation byte.
	CHECK_FALSE(IsValidUtf8("\xC4Z"));

	// Overlong forms: '/' encoded in two bytes, and NUL encoded in two.
	CHECK_FALSE(IsValidUtf8("\xC0\xAF"));
	CHECK_FALSE(IsValidUtf8("\xC0\x80"));

	// A surrogate half, which UTF-8 may not carry.
	CHECK_FALSE(IsValidUtf8("\xED\xA0\x80"));

	// Above U+10FFFF.
	CHECK_FALSE(IsValidUtf8("\xF5\x80\x80\x80"));

	// A five-byte lead byte, which UTF-8 has never had.
	CHECK_FALSE(IsValidUtf8("\xF8\x80\x80\x80\x80"));
}

TEST_CASE("A Windows-1250 file is not valid UTF-8")
{
	// "część" as Windows-1250 writes it. This has to fail validation, which is what
	// makes the document read-only rather than silently mixed on the first keystroke.
	CHECK_FALSE(IsValidUtf8("cz\xEA\x9C\xE6"));
}

TEST_CASE("Stepping forward crosses a whole character")
{
	const std::string text = "a\xC4\x99\xF0\x9F\x99\x82z"; // a ę 🙂 z

	CHECK(NextCharacter(text, 0) == 1); // over 'a'
	CHECK(NextCharacter(text, 1) == 3); // over ę, two bytes
	CHECK(NextCharacter(text, 3) == 7); // over 🙂, four bytes
	CHECK(NextCharacter(text, 7) == 8); // over 'z'
}

TEST_CASE("Stepping backward crosses a whole character")
{
	const std::string text = "a\xC4\x99\xF0\x9F\x99\x82z";

	CHECK(PreviousCharacter(text, 8) == 7);
	CHECK(PreviousCharacter(text, 7) == 3);
	CHECK(PreviousCharacter(text, 3) == 1);
	CHECK(PreviousCharacter(text, 1) == 0);
}

TEST_CASE("Stepping stops at the ends instead of running off them")
{
	const std::string text = "ab";

	CHECK(NextCharacter(text, 2) == 2);
	CHECK(NextCharacter(text, 99) == 2);
	CHECK(PreviousCharacter(text, 0) == 0);
	CHECK(PreviousCharacter("", 0) == 0);
}

TEST_CASE("Stepping out of the middle of a character lands on a boundary")
{
	// Nothing should ever hand us an offset inside a sequence, but if something does,
	// the answer has to be a boundary rather than another interior byte.
	const std::string text = "\xF0\x9F\x99\x82";

	CHECK(PreviousCharacter(text, 2) == 0);
	CHECK(NextCharacter(text, 1) == 4);
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
	CHECK(BomBytes(Encoding::Utf8Bom) == std::string_view{"\xEF\xBB\xBF", 3});
	CHECK(BomBytes(Encoding::Utf16Le) == std::string_view{"\xFF\xFE", 2});
	CHECK(BomBytes(Encoding::Utf16Be) == std::string_view{"\xFE\xFF", 2});
	CHECK(BomBytes(Encoding::Utf8).empty());
	CHECK(BomBytes(Encoding::Unknown).empty());
}

TEST_CASE("A mark's length matches the bytes it is made of")
{
	// BomLength reads a file's leading bytes and BomBytes writes them; if they ever
	// disagree, a file gains or loses bytes each time it is opened and saved.
	for (const Encoding encoding :
		 {Encoding::Utf8, Encoding::Utf8Bom, Encoding::Utf16Le, Encoding::Utf16Be})
	{
		const std::string_view mark = BomBytes(encoding);

		CHECK(BomLength(std::string{mark} + "rest") == mark.size());
	}
}

TEST_CASE("A continuation byte is exactly one of 0x80 to 0xBF, read without sign extension")
{
	for (int byteValue = 0; byteValue < 256; ++byteValue)
	{
		const bool expected = byteValue >= 0x80 && byteValue <= 0xBF;

		CHECK(IsUtf8ContinuationByte(static_cast<char>(byteValue)) == expected);
	}

	static_assert(IsUtf8ContinuationByte('\x80'));
	static_assert(IsUtf8ContinuationByte('\xBF'));
	static_assert(!IsUtf8ContinuationByte('\xC0'));
	static_assert(!IsUtf8ContinuationByte('\0'));
}

TEST_CASE("A lead byte's claimed length, and one for any byte that cannot lead")
{
	for (int byteValue = 0; byteValue < 256; ++byteValue)
	{
		std::size_t expected = 1;

		if (byteValue >= 0xC0 && byteValue <= 0xDF)
		{
			expected = 2;
		}
		else if (byteValue >= 0xE0 && byteValue <= 0xEF)
		{
			expected = 3;
		}
		else if (byteValue >= 0xF0 && byteValue <= 0xF7)
		{
			expected = 4;
		}

		CHECK(Utf8SequenceLength(static_cast<char>(byteValue)) == expected);
	}

	// Unlike DecodeUtf8, the overlong leads and F5 to F7 are taken at their word.
	CHECK(Utf8SequenceLength('\xC0') == 2);
	CHECK(Utf8SequenceLength('\xF5') == 4);
	CHECK(Utf8SequenceLength('\x80') == 1);
	CHECK(Utf8SequenceLength('\xFF') == 1);
}

TEST_CASE("CharacterBoundaryAtOrBefore backs off to a character's first byte and clamps to the end")
{
	const std::string_view text = "a\xC4\x99";

	CHECK(CharacterBoundaryAtOrBefore(text, 0) == 0);
	CHECK(CharacterBoundaryAtOrBefore(text, 1) == 1);
	CHECK(CharacterBoundaryAtOrBefore(text, 2) == 1);
	CHECK(CharacterBoundaryAtOrBefore(text, 3) == 3);
	CHECK(CharacterBoundaryAtOrBefore(text, 99) == 3);

	CHECK(CharacterBoundaryAtOrBefore("", 0) == 0);
	CHECK(CharacterBoundaryAtOrBefore("", 5) == 0);

	// Stray continuation bytes back off all the way to the start, never past it.
	CHECK(CharacterBoundaryAtOrBefore("\x80\x80", 1) == 0);
	CHECK(CharacterBoundaryAtOrBefore("\x80\x80", 2) == 2);
}

TEST_CASE("CharacterBoundaryAtOrAfter skips continuation bytes and leaves an offset past the end alone")
{
	const std::string_view text = "a\xC4\x99"
								  "b";

	CHECK(CharacterBoundaryAtOrAfter(text, 1) == 1);
	CHECK(CharacterBoundaryAtOrAfter(text, 2) == 3);
	CHECK(CharacterBoundaryAtOrAfter(text, 4) == 4);
	CHECK(CharacterBoundaryAtOrAfter(text, 9) == 9);
	CHECK(CharacterBoundaryAtOrAfter("", 0) == 0);
	CHECK(CharacterBoundaryAtOrAfter("", 1) == 1);
	CHECK(CharacterBoundaryAtOrAfter("\x80\x80", 0) == 2);
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
