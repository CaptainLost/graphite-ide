#include <doctest/doctest.h>

#include "Core/Sln/SolutionEditor.h"

#include <set>
#include <string>

TEST_CASE("A generated identifier has the shape both solution formats expect")
{
    const std::string guid = core::sln::NewGuid();

    REQUIRE(guid.size() == 38);
    CHECK(guid.front() == '{');
    CHECK(guid.back() == '}');
    CHECK(guid[9] == '-');
    CHECK(guid[14] == '-');
    CHECK(guid[19] == '-');
    CHECK(guid[24] == '-');
}

TEST_CASE("Every character is an uppercase hexadecimal digit or a separator")
{
    const std::string guid = core::sln::NewGuid();

    for (std::size_t i = 1; i + 1 < guid.size(); ++i)
    {
        const char character = guid[i];

        if (character == '-')
            continue;

        // Visual Studio writes them uppercase, and a solution carrying both spellings is a
        // diff nobody asked for.
        const bool digit = character >= '0' && character <= '9';
        const bool upper = character >= 'A' && character <= 'F';

        CHECK((digit || upper));
    }
}

TEST_CASE("An identifier is never empty")
{
    // The previous implementation returned an empty string wherever CoCreateGuid was not
    // available, which made every caller carry a check for a case it could not handle —
    // and made adding a project impossible on any other system, for no reason to do with
    // adding projects.
    CHECK_FALSE(core::sln::NewGuid().empty());
}

TEST_CASE("A thousand identifiers in a row are all different")
{
    std::set<std::string> seen;

    for (int i = 0; i < 1000; ++i)
        seen.insert(core::sln::NewGuid());

    // Far below where a collision would be expected, and far above where a fixed seed or
    // one taken per call from a low-resolution clock would repeat itself.
    CHECK(seen.size() == 1000);
}

TEST_CASE("The version and variant bits say what kind of identifier this is")
{
    const std::string guid = core::sln::NewGuid();

    CHECK(guid[15] == '4');

    const char variant = guid[20];
    CHECK((variant == '8' || variant == '9' || variant == 'A' || variant == 'B'));
}
