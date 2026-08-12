#include <doctest/doctest.h>

#include "Core/Base/Uuid.h"

#include <set>
#include <string>

TEST_CASE("A generated identifier has the canonical undecorated shape")
{
    const std::string text = Core::Uuid::Generate().ToString();

    REQUIRE(text.size() == 36);
    CHECK(text[8] == '-');
    CHECK(text[13] == '-');
    CHECK(text[18] == '-');
    CHECK(text[23] == '-');

    // Nothing wraps it. A format that wants braces adds them, which is what keeps the
    // solution formats' spelling out of this class.
    CHECK(text.front() != '{');
    CHECK(text.back() != '}');
}

TEST_CASE("Every character is an uppercase hexadecimal digit or a separator")
{
    const std::string text = Core::Uuid::Generate().ToString();

    for (const char character : text)
    {
        if (character == '-')
            continue;

        // Visual Studio writes them uppercase, and a solution carrying both spellings is a
        // diff nobody asked for.
        const bool digit = character >= '0' && character <= '9';
        const bool upper = character >= 'A' && character <= 'F';

        CHECK((digit || upper));
    }
}

TEST_CASE("The version and variant bits say what kind of identifier this is")
{
    const auto& bytes = Core::Uuid::Generate().GetBytes();

    // Checked on the bytes rather than on the text, because the bytes are where RFC 4122
    // puts these fields — byte 6's top nibble and byte 8's top two bits.
    CHECK((bytes[6] & 0xF0) == 0x40);
    CHECK((bytes[8] & 0xC0) == 0x80);
}

TEST_CASE("The text says the same thing the bytes do")
{
    const Core::Uuid uuid = Core::Uuid::Generate();
    const std::string text = uuid.ToString();

    // The version nibble is the first digit of the third group, the variant the first of
    // the fourth. A grouping mistake in ToString would move them and nothing else would
    // notice.
    CHECK(text[14] == '4');

    const char variant = text[19];
    CHECK((variant == '8' || variant == '9' || variant == 'A' || variant == 'B'));
}

TEST_CASE("A thousand identifiers in a row are all different")
{
    std::set<std::string> seen;

    for (int i = 0; i < 1000; ++i)
        seen.insert(Core::Uuid::Generate().ToString());

    // Far below where a collision would be expected, and far above where a fixed seed or
    // one taken per call from a low-resolution clock would repeat itself.
    CHECK(seen.size() == 1000);
}
