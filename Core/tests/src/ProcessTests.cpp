#include "corepch.h"

#include "Core/Platform/Process.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace core;

TEST_CASE("The executable directory exists and holds the running test binary")
{
    const auto directory = platform::ExecutableDirectory();

    REQUIRE(directory.HasValue());
    CHECK(std::filesystem::is_directory(directory.Value()));

    // Naming the binary rather than just checking for a directory: a function that
    // returned the working directory would pass the weaker assertion, and the working
    // directory is exactly what this exists to stop using.
    CHECK(std::filesystem::exists(directory.Value() / "CoreTests.exe"));
}

TEST_CASE("The executable directory is absolute")
{
    const auto directory = platform::ExecutableDirectory();

    REQUIRE(directory.HasValue());
    CHECK(directory.Value().is_absolute());
}
