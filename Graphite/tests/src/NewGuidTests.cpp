#include <doctest/doctest.h>

#include "Sln/SolutionEditor.h"

#include <string>

// What a UUID is and what it guarantees is covered by UuidTests. What is left here is the
// one thing this function adds: the braced spelling both solution formats use.

TEST_CASE("A generated identifier has the shape both solution formats expect")
{
    const std::string guid = Graphite::NewGuid();

    REQUIRE(guid.size() == 38);
    CHECK(guid.front() == '{');
    CHECK(guid.back() == '}');
    CHECK(guid[9] == '-');
    CHECK(guid[14] == '-');
    CHECK(guid[19] == '-');
    CHECK(guid[24] == '-');
}

TEST_CASE("An identifier is never empty")
{
    // The previous implementation returned an empty string wherever CoCreateGuid was not
    // available, which made every caller carry a check for a case it could not handle —
    // and made adding a project impossible on any other system, for no reason to do with
    // adding projects.
    CHECK_FALSE(Graphite::NewGuid().empty());
}
