#include <doctest/doctest.h>

#include "Core/Text/Encoding.h"

#include <ostream>
#include <string>

using namespace core::text;

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
    CHECK(IsValidUtf8("cz\xC4\x99\xC5\x9B\xC4\x87"));      // częś in UTF-8
    CHECK(IsValidUtf8("\xD0\x9F\xD1\x80\xD0\xB8"));        // При
    CHECK(IsValidUtf8("\xE6\x97\xA5\xE6\x9C\xAC"));        // 日本
    CHECK(IsValidUtf8("\xF0\x9F\x99\x82"));                // 🙂
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
    const std::string text = "a\xC4\x99\xF0\x9F\x99\x82z";  // a ę 🙂 z

    CHECK(NextCharacter(text, 0) == 1);   // over 'a'
    CHECK(NextCharacter(text, 1) == 3);   // over ę, two bytes
    CHECK(NextCharacter(text, 3) == 7);   // over 🙂, four bytes
    CHECK(NextCharacter(text, 7) == 8);   // over 'z'
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
