#include <doctest/doctest.h>

#include "Core/Text/Codec.h"

#include <string>

using namespace core::text;

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
    CHECK(decoded.Value() == "plain text");
}

TEST_CASE("Unknown passes through decoding untouched")
{
    // Bytes that are not valid UTF-8 — "część" as Windows-1250 writes it.
    const std::string bytes = "cz\xEA\x9C\xE6";
    const auto decoded = Decode(bytes, Encoding::Unknown);

    REQUIRE(decoded);
    CHECK(decoded.Value() == bytes);
}

TEST_CASE("UTF-16 decodes in both byte orders")
{
    const auto le = Decode(kHiLe, Encoding::Utf16Le);
    const auto be = Decode(kHiBe, Encoding::Utf16Be);

    REQUIRE(le);
    REQUIRE(be);
    CHECK(le.Value() == "Hi");
    CHECK(be.Value() == "Hi");
}

TEST_CASE("An empty file decodes to nothing rather than failing")
{
    const auto decoded = Decode("", Encoding::Utf16Le);

    REQUIRE(decoded);
    CHECK(decoded.Value() == "");
}

TEST_CASE("Characters outside the basic plane decode from a surrogate pair")
{
    // U+1F642 is D83D DE42; little-endian that is 3D D8 42 DE.
    const std::string bytes = "\x3D\xD8\x42\xDE";
    const auto decoded = Decode(bytes, Encoding::Utf16Le);

    REQUIRE(decoded);
    CHECK(decoded.Value() == "\xF0\x9F\x99\x82");
}

TEST_CASE("Polish characters survive the round trip in both byte orders")
{
    const std::string text = "zaz\xC3\xB3\xC5\x82\xC4\x87 g\xC4\x99\xC5\x9Bl\xC4\x85";

    for (const Encoding encoding : {Encoding::Utf16Le, Encoding::Utf16Be})
    {
        const std::string encoded = Encode(text, encoding);
        const auto decoded = Decode(encoded, encoding);

        REQUIRE(decoded);
        CHECK(decoded.Value() == text);
    }
}

TEST_CASE("An emoji survives the round trip, which is the only case that makes a pair")
{
    const std::string text = "a\xF0\x9F\x99\x82z";

    for (const Encoding encoding : {Encoding::Utf16Le, Encoding::Utf16Be})
    {
        const auto decoded = Decode(Encode(text, encoding), encoding);

        REQUIRE(decoded);
        CHECK(decoded.Value() == text);
    }
}

TEST_CASE("Bytes decoded and re-encoded come back identical")
{
    const auto decoded = Decode(kHiLe, Encoding::Utf16Le);

    REQUIRE(decoded);
    CHECK(Encode(decoded.Value(), Encoding::Utf16Le) == kHiLe);
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
