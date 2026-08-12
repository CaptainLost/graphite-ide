#include <doctest/doctest.h>

#include "Core/Text/Codec.h"

#include <string>

using namespace Core;

namespace
{
    /// "Hi" in UTF-16, little-endian: 48 00 69 00.
    const std::string kHiLe = std::string("H\0i\0", 4);
    /// The same big-endian: 00 48 00 69.
    const std::string kHiBe = std::string("\0H\0i", 4);
}

TEST_CASE("UTF-8 passes through decoding untouched")
{
    const auto decoded = Decode("plain text", Encoding::Utf8);

    REQUIRE(decoded);
    CHECK(decoded.GetValue() == "plain text");
}

TEST_CASE("Unknown passes through decoding untouched")
{
    // Bytes that are not valid UTF-8 — "część" as Windows-1250 writes it.
    const std::string bytes = "cz\xEA\x9C\xE6";
    const auto decoded = Decode(bytes, Encoding::Unknown);

    REQUIRE(decoded);
    CHECK(decoded.GetValue() == bytes);
}

TEST_CASE("UTF-16 decodes in both byte orders")
{
    const auto le = Decode(kHiLe, Encoding::Utf16Le);
    const auto be = Decode(kHiBe, Encoding::Utf16Be);

    REQUIRE(le);
    REQUIRE(be);
    CHECK(le.GetValue() == "Hi");
    CHECK(be.GetValue() == "Hi");
}

TEST_CASE("An empty file decodes to nothing rather than failing")
{
    const auto decoded = Decode("", Encoding::Utf16Le);

    REQUIRE(decoded);
    CHECK(decoded.GetValue() == "");
}

TEST_CASE("Characters outside the basic plane decode from a surrogate pair")
{
    // U+1F642 is D83D DE42; little-endian that is 3D D8 42 DE.
    const std::string bytes = "\x3D\xD8\x42\xDE";
    const auto decoded = Decode(bytes, Encoding::Utf16Le);

    REQUIRE(decoded);
    CHECK(decoded.GetValue() == "\xF0\x9F\x99\x82");
}

TEST_CASE("Polish characters survive the round trip in both byte orders")
{
    const std::string text = "zaz\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85";

    for (const Encoding encoding : {Encoding::Utf16Le, Encoding::Utf16Be})
    {
        const std::string encoded = Encode(text, encoding);
        const auto decoded = Decode(encoded, encoding);

        REQUIRE(decoded);
        CHECK(decoded.GetValue() == text);
    }
}

TEST_CASE("An emoji survives the round trip, which is the only case that makes a pair")
{
    const std::string text = "a\xF0\x9F\x99\x82z";

    for (const Encoding encoding : {Encoding::Utf16Le, Encoding::Utf16Be})
    {
        const auto decoded = Decode(Encode(text, encoding), encoding);

        REQUIRE(decoded);
        CHECK(decoded.GetValue() == text);
    }
}

TEST_CASE("Bytes decoded and re-encoded come back identical")
{
    const auto decoded = Decode(kHiLe, Encoding::Utf16Le);

    REQUIRE(decoded);
    CHECK(Encode(decoded.GetValue(), Encoding::Utf16Le) == kHiLe);
}

TEST_CASE("An odd number of bytes is a truncated file, not a character")
{
    const auto decoded = Decode(std::string("H\0i", 3), Encoding::Utf16Le);

    REQUIRE_FALSE(decoded);
    CHECK(decoded.GetError().message.find("odd") != std::string::npos);
}

TEST_CASE("A high surrogate without a low one fails")
{
    // D83D followed by an ordinary character rather than its partner.
    // Built with an explicit length: a string literal would stop at the trailing NUL.
    const std::string bytes = std::string("\x3D\xD8\x69\x00", 4);
    const auto decoded = Decode(bytes, Encoding::Utf16Le);

    REQUIRE_FALSE(decoded);
    CHECK(decoded.GetError().message.find("surrogate") != std::string::npos);
}

TEST_CASE("A high surrogate at the very end fails")
{
    const std::string bytes = "\x3D\xD8";
    const auto decoded = Decode(bytes, Encoding::Utf16Le);

    REQUIRE_FALSE(decoded);
    CHECK(decoded.GetError().message.find("surrogate") != std::string::npos);
}

TEST_CASE("A low surrogate with nothing before it fails")
{
    const std::string bytes = "\x42\xDE";
    const auto decoded = Decode(bytes, Encoding::Utf16Le);

    REQUIRE_FALSE(decoded);
    CHECK(decoded.GetError().message.find("surrogate") != std::string::npos);
}

TEST_CASE("The error names where it happened, so a damaged file can be looked at")
{
    // "Hi" then a stray low surrogate, so the failure is at byte 4 rather than byte 0.
    // Built with an explicit length: a string literal would stop at the first NUL.
    const std::string bytes = std::string("H\0i\0\x42\xDE", 6);
    const auto decoded = Decode(bytes, Encoding::Utf16Le);

    REQUIRE_FALSE(decoded);
    CHECK(decoded.GetError().message.find("4") != std::string::npos);
}

TEST_CASE("Encoding passes UTF-8, its BOM variant and Unknown through unchanged")
{
    // This is what keeps a read-only file whose bytes failed validation byte-identical
    // when it is written back.
    const std::string bytes = "cz\xEA\x9C\xE6";

    CHECK(Encode(bytes, Encoding::Utf8) == bytes);
    CHECK(Encode(bytes, Encoding::Utf8Bom) == bytes);
    CHECK(Encode(bytes, Encoding::Unknown) == bytes);
}

TEST_CASE("Encoding writes the byte order it was asked for")
{
    CHECK(Encode("Hi", Encoding::Utf16Le) == kHiLe);
    CHECK(Encode("Hi", Encoding::Utf16Be) == kHiBe);
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
    // "Hello" — NULs at odd positions is little-endian, at even is big-endian.
    CHECK(SniffEncoding(std::string("H\0e\0l\0l\0o\0", 10)) == Encoding::Utf16Le);
    CHECK(SniffEncoding(std::string("\0H\0e\0l\0l\0o", 10)) == Encoding::Utf16Be);
}

TEST_CASE("A file with NUL bytes on both parities is not UTF-16")
{
    // Both parities carry NULs, so neither byte order explains the file.
    const std::string bytes = std::string("\0\0a\0\0b\0\0", 8);

    CHECK(SniffEncoding(bytes) == Encoding::Utf8);
}

TEST_CASE("A single NUL byte is not enough evidence")
{
    CHECK(SniffEncoding(std::string("ab\0d", 4)) == Encoding::Utf8);
}

TEST_CASE("Binary data whose NULs happen to line up is caught by the decode check")
{
    // Every NUL at an odd position, so the parity test passes — but decoded as UTF-16LE
    // these are control characters, which text does not contain. This is the case that
    // keeps the third condition from being decoration.
    const std::string bytes = std::string("\x01\0\x02\0\x03\0\x04\0", 8);

    CHECK(SniffEncoding(bytes) == Encoding::Utf8);
}

TEST_CASE("Text containing tabs and newlines is still text")
{
    const std::string bytes = std::string("a\0\t\0\n\0b\0", 8);

    CHECK(SniffEncoding(bytes) == Encoding::Utf16Le);
}

TEST_CASE("An unpaired surrogate means the bytes are not UTF-16 text")
{
    // A high surrogate followed by an ordinary character: NULs line up, but it does not
    // decode.
    const std::string bytes = std::string("\x3D\xD8\x69\0\x69\0", 6);

    CHECK(SniffEncoding(bytes) == Encoding::Utf8);
}

TEST_CASE("A sample cut mid-unit is still recognised")
{
    // Nine bytes: four whole units and one stray byte, as an 8 KB sample of a larger
    // file would be.
    const std::string bytes = std::string("H\0e\0l\0l\0o", 9);

    CHECK(SniffEncoding(bytes) == Encoding::Utf16Le);
}

TEST_CASE("A character outside Latin-1 does not break the parity rule")
{
    // U+0100 is 00 01 little-endian, putting one NUL at the even parity while every
    // ASCII character puts one at the odd. Nine ASCII characters against that one give
    // 9 of 10 NULs at the dominant parity, which is exactly the threshold — so this case
    // also pins where the threshold sits. Note the arithmetic: fewer than nine ASCII
    // characters here and the file would be called UTF-8.
    const std::string bytes = std::string("a\0b\0c\0d\0e\0f\0g\0h\0i\0\0\x01", 20);

    CHECK(SniffEncoding(bytes) == Encoding::Utf16Le);
}

TEST_CASE("Decoded C1 control characters are not text")
{
    // Codepoints U+0085, U+0086, U+0087, U+0088 — all C1 controls, all NULs at the odd
    // parity. Their UTF-8 encoding is C2 85, C2 86, C2 87, C2 88, whose bytes are all
    // individually >= 0x20, which is exactly what makes them easy to miss.
    const std::string bytes = std::string("\x85\0\x86\0\x87\0\x88\0", 8);

    CHECK(SniffEncoding(bytes) == Encoding::Utf8);
}

TEST_CASE("Decoded DEL is not text")
{
    // Codepoints U+007F 'A' U+007F 'B' — DEL is a single UTF-8 byte >= 0x20, so it looks
    // like an ordinary character unless matched specifically.
    const std::string bytes = std::string("\x7F\0\x41\0\x7F\0\x42\0", 8);

    CHECK(SniffEncoding(bytes) == Encoding::Utf8);
}
