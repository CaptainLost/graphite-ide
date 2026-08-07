#include <doctest/doctest.h>

#include "Core/Fs/FileOperations.h"

#include "TempDirectory.h"

using core::fs::IsValidFileName;
using core::fs::MakeUniquePath;

TEST_CASE("MakeUniquePath returns the original path when nothing collides")
{
    TempDirectory temp;
    const auto free = temp.Path() / "main.cpp";

    CHECK(MakeUniquePath(free) == free);
}

TEST_CASE("MakeUniquePath appends copy on the first collision")
{
    TempDirectory temp;
    temp.MakeFile("main.cpp", "x");

    CHECK(MakeUniquePath(temp.Path() / "main.cpp") == temp.Path() / "main copy.cpp");
}

TEST_CASE("MakeUniquePath numbers further collisions")
{
    TempDirectory temp;
    temp.MakeFile("main.cpp", "x");
    temp.MakeFile("main copy.cpp", "x");
    temp.MakeFile("main copy 2.cpp", "x");

    CHECK(MakeUniquePath(temp.Path() / "main.cpp") == temp.Path() / "main copy 3.cpp");
}

TEST_CASE("MakeUniquePath handles a name without an extension")
{
    TempDirectory temp;
    temp.MakeDir("build");

    CHECK(MakeUniquePath(temp.Path() / "build") == temp.Path() / "build copy");
}

TEST_CASE("MakeUniquePath treats a dotfile as a name, not an extension")
{
    TempDirectory temp;
    temp.MakeFile(".gitignore", "x");

    CHECK(MakeUniquePath(temp.Path() / ".gitignore") == temp.Path() / ".gitignore copy");
}

TEST_CASE("MakeUniquePath splits a compound extension the way std::filesystem does")
{
    TempDirectory temp;
    temp.MakeFile("archive.tar.gz", "x");

    // stem() is "archive.tar", extension() is ".gz" — consistency with the standard
    // library beats intuition about double extensions.
    CHECK(MakeUniquePath(temp.Path() / "archive.tar.gz") == temp.Path() / "archive.tar copy.gz");
}

TEST_CASE("IsValidFileName accepts ordinary names")
{
    CHECK(IsValidFileName("main.cpp"));
    CHECK(IsValidFileName(".gitignore"));
    CHECK(IsValidFileName("a name with spaces.txt"));
    CHECK(IsValidFileName("CONSOLE.txt"));
    CHECK(IsValidFileName("COM10.txt"));
}

TEST_CASE("IsValidFileName rejects an empty or blank name")
{
    CHECK_FALSE(IsValidFileName(""));
    CHECK_FALSE(IsValidFileName("   "));
}

TEST_CASE("IsValidFileName rejects characters the filesystem forbids")
{
    CHECK_FALSE(IsValidFileName("a/b"));
    CHECK_FALSE(IsValidFileName("a\\b"));
    CHECK_FALSE(IsValidFileName("a:b"));
    CHECK_FALSE(IsValidFileName("a*b"));
    CHECK_FALSE(IsValidFileName("a?b"));
    CHECK_FALSE(IsValidFileName("a\"b"));
    CHECK_FALSE(IsValidFileName("a<b"));
    CHECK_FALSE(IsValidFileName("a>b"));
    CHECK_FALSE(IsValidFileName("a|b"));
}

TEST_CASE("IsValidFileName rejects a trailing dot or space")
{
    CHECK_FALSE(IsValidFileName("name."));
    CHECK_FALSE(IsValidFileName("name "));
}

TEST_CASE("IsValidFileName rejects reserved device names, with or without an extension")
{
    CHECK_FALSE(IsValidFileName("CON"));
    CHECK_FALSE(IsValidFileName("con"));
    CHECK_FALSE(IsValidFileName("NUL.txt"));
    CHECK_FALSE(IsValidFileName("PRN"));
    CHECK_FALSE(IsValidFileName("AUX"));
    CHECK_FALSE(IsValidFileName("COM1"));
    CHECK_FALSE(IsValidFileName("COM9.cpp"));
    CHECK_FALSE(IsValidFileName("LPT1"));
    CHECK_FALSE(IsValidFileName("lpt9.log"));
}
